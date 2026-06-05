/**
 * @file host_runtime.cpp
 * @brief 主机运行时环境实现
 *
 * @details 该文件实现了 HostRuntime 类的所有成员函数，包括模块生命周期管理
 *          （注册、启动、停止、等待退出）和运行时状态快照导出。
 *          还包含一个内部辅助函数 escape_json() 用于 JSON 字符串转义。
 *
 * 生命周期管理策略：
 * - 启动：按注册顺序正向执行
 * - 停止：按注册顺序逆向执行，但 PersistenceService 具有最高优先级最后停止
 * - 等待：确保所有模块线程完全退出后再释放资源
 *
 * 状态快照功能用于：
 * - 运维监控（Prometheus 等工具采集）
 * - 调试分析（查看各模块内部状态）
 * - 健康检查（确认各组件运行正常）
 */

#include "core/host_runtime.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace mir2 {

namespace {

/**
 * @brief 转义字符串中的特殊字符，使其适合嵌入 JSON 字符串值
 * @param value 原始字符串
 * @return 转义后的字符串
 *
 * @details 处理以下 JSON 特殊字符：
 *          - 反斜杠 '\\' -> "\\\\"
 *          - 双引号 '"'  -> "\\\""
 *          - 换行符 '\n' -> "\\n"
 *
 *          JSON 序列化采用手动实现而非引入第三方库，以减少依赖并保持
 *          代码体积最小化。此函数仅处理字符串值中必需的转义，
 *          不处理 Unicode 转义等不常用情况。
 */
std::string escape_json(std::string value) {
  std::string escaped;
  escaped.reserve(value.size());  // 预分配，多数情况无需扩容
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

}  // namespace

/**
 * @brief 构造函数：初始化运行时环境
 * @param root_dir 服务器根目录路径
 * @param config 主机配置
 * @param logger spdlog 日志记录器共享指针
 *
 * @details 初始化过程：
 * 1. 保存根目录路径到成员变量和上下文中
 * 2. 保存配置到上下文中
 * 3. 将内部组件的指针注入到上下文中：
 *    - bus：消息总线，用于模块间通信
 *    - metrics：度量注册表，用于指标收集
 *    - shutdown：关闭令牌，用于协调优雅退出
 *    - logger：日志记录器，用于日志输出
 *
 * 这种设计使得所有模块可以通过 HostContext 访问这些共享资源，
 * 而无需直接引用 HostRuntime 本身，降低了耦合度。
 */
HostRuntime::HostRuntime(std::filesystem::path root_dir, HostConfig config,
                         std::shared_ptr<spdlog::logger> logger)
    : root_dir_(std::move(root_dir)) {
  context_.root_dir = root_dir_;
  context_.config = std::move(config);
  context_.bus = &bus_;
  context_.metrics = &metrics_;
  context_.shutdown = &shutdown_;
  context_.logger = std::move(logger);
}

/**
 * @brief 析构函数：安全停止所有模块并释放资源
 *
 * @details 析构时确保：
 * 1. 调用 stop_all() 请求所有模块停止
 * 2. 调用 join_all() 等待所有模块线程退出
 * 3. 异常安全：任何模块抛出的异常都不会传播到析构函数之外
 *
 * @warning 如果模块的 stop() 或 join() 抛出异常，将被析构函数捕获并忽略。
 *          这可能导致模块资源未完全释放，但避免了程序因析构函数异常而终止。
 */
HostRuntime::~HostRuntime() {
  try {
    stop_all();
    join_all();
  } catch (...) {
    // 析构函数中不允许异常传播，捕获所有异常
  }
}

/**
 * @brief 注册模块到内部列表
 * @param module 模块的唯一指针
 *
 * @details 将模块添加到 modules_ 向量末尾。
 *          模块按注册顺序存储，后续的启动/停止操作依赖此顺序。
 *          注册操作本身是轻量级的，不会启动模块。
 */
void HostRuntime::register_module(std::unique_ptr<Module> module) { modules_.push_back(std::move(module)); }

/**
 * @brief 按注册顺序启动所有模块
 *
 * @details 遍历模块列表，对每个模块调用 start() 方法，传入运行时上下文。
 *          启动顺序的设计考虑：
 *          - LogService 最先注册，确保日志功能最早可用
 *          - PersistenceService 紧随其后，使后续模块可以访问数据库
 *          - 网关服务最后注册，避免客户端过早连接
 */
void HostRuntime::start_all() {
  for (auto& module : modules_) {
    module->start(context_);
  }
}

/**
 * @brief 按依赖关系逆序停止所有模块
 *
 * @details 停止策略分为三个步骤：
 *
 * 步骤一：触发关闭令牌
 * - 调用 shutdown_.request_stop()，通知所有模块服务即将关闭
 * - 模块可以在下次循环检查中检测到关闭信号并自行退出
 *
 * 步骤二：停止非持久化模块（逆序）
 * - 从最后一个注册的模块开始反向遍历
 * - 跳过 PersistenceService（持久化服务），将其留在最后
 * - 对每个模块依次调用 stop() 和 join()
 *
 * 步骤三：停止持久化模块
 * - 最后停止 PersistenceService
 * - 确保其他模块在关闭过程中仍可访问数据库
 * - 允许其他模块在退出前保存关键状态
 *
 * 步骤四：关闭消息总线
 * - 释放所有队列资源，唤醒仍在等待的消费者
 *
 * @see ShutdownToken 用于协调关闭信号
 * @see PersistenceService 最后停止的设计
 */
void HostRuntime::stop_all() {
  shutdown_.request_stop();
  // 第一轮：逆序停止非持久化模块
  for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
    if ((*it)->name() == "persistence_service") {
      continue;
    }
    (*it)->stop();
    (*it)->join();
  }
  // 第二轮：最后停止持久化模块
  for (auto& module : modules_) {
    if (module->name() != "persistence_service") {
      continue;
    }
    module->stop();
    module->join();
  }
  bus_.close_all();
}

/**
 * @brief 等待所有模块线程完全退出
 *
 * @details 对每个模块调用 join() 方法，等待其内部线程执行完毕。
 *          通常在 stop_all() 之后或析构函数中调用。
 *          如果模块没有使用独立线程（如纯事件驱动模块），join() 应为空操作。
 */
void HostRuntime::join_all() {
  for (auto& module : modules_) {
    module->join();
  }
}

/**
 * @brief 将运行时状态写入 JSON 快照文件
 *
 * @details 生成 JSON 格式的状态报告，包含三个主要部分：
 *
 * 1. "queues" —— 消息队列深度
 *    - 每个消息总线端点的当前待处理消息数量
 *    - 可用于检测消息积压或处理瓶颈
 *
 * 2. "metrics" —— 运行时度量数据
 *    - 所有计数器和仪表盘的当前数值快照
 *    - 包括请求数、错误数、延迟等指标
 *
 * 3. "modules" —— 模块状态
 *    - 每个模块自定义的状态信息
 *    - 通过 Module::snapshot() 接口获取
 *
 * JSON 文件输出路径由配置项 runtime.status_file 指定，
 * 相对于 root_dir_。如果输出目录不存在则自动创建。
 *
 * @note 字符串值使用 escape_json() 函数进行转义，防止 JSON 格式被破坏。
 *       数字值直接输出，不进行转义处理。
 */
void HostRuntime::write_status_snapshot() const {
  const auto status_path = root_dir_ / context_.config.runtime.status_file;
  std::filesystem::create_directories(status_path.parent_path());

  std::ostringstream out;
  out << "{\n";
  // ---- 第一部分：队列深度 ----
  out << "  \"queues\": {\n";
  const auto queues = bus_.queue_depths();
  bool first = true;
  for (const auto& [name, depth] : queues) {
    if (!first) {
      out << ",\n";
    }
    first = false;
    out << "    \"" << escape_json(name) << "\": " << depth;
  }
  out << "\n  },\n";

  // ---- 第二部分：度量数据 ----
  out << "  \"metrics\": {\n";
  const auto metrics_snapshot = metrics_.snapshot();
  first = true;
  for (const auto& [name, value] : metrics_snapshot) {
    if (!first) {
      out << ",\n";
    }
    first = false;
    out << "    \"" << escape_json(name) << "\": " << value;
  }
  out << "\n  },\n";

  // ---- 第三部分：模块状态 ----
  out << "  \"modules\": {\n";
  first = true;
  for (const auto& module : modules_) {
    if (!first) {
      out << ",\n";
    }
    first = false;
    out << "    \"" << escape_json(module->name()) << "\": {";
    const auto module_snapshot = module->snapshot();
    bool first_field = true;
    for (const auto& [key, value] : module_snapshot) {
      if (!first_field) {
        out << ", ";
      }
      first_field = false;
      out << "\"" << escape_json(key) << "\": \"" << escape_json(value) << "\"";
    }
    out << "}";
  }
  out << "\n  }\n";
  out << "}\n";

  // 写入文件（二进制模式 + 截断模式）
  std::ofstream file(status_path, std::ios::binary | std::ios::trunc);
  file << out.str();
}

}  // namespace mir2
