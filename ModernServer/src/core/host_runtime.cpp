#include "core/host_runtime.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace mir2 {

namespace {

std::string escape_json(std::string value) {
  std::string escaped;
  escaped.reserve(value.size());
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

HostRuntime::~HostRuntime() {
  try {
    stop_all();
    join_all();
  } catch (...) {
  }
}

void HostRuntime::register_module(std::unique_ptr<Module> module) { modules_.push_back(std::move(module)); }

void HostRuntime::start_all() {
  for (auto& module : modules_) {
    module->start(context_);
  }
}

void HostRuntime::stop_all() {
  shutdown_.request_stop();
  bus_.close_all();
  for (auto& module : modules_) {
    module->stop();
  }
}

void HostRuntime::join_all() {
  for (auto& module : modules_) {
    module->join();
  }
}

void HostRuntime::write_status_snapshot() const {
  const auto status_path = root_dir_ / context_.config.runtime.status_file;
  std::filesystem::create_directories(status_path.parent_path());

  std::ostringstream out;
  out << "{\n";
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

  std::ofstream file(status_path, std::ios::binary | std::ios::trunc);
  file << out.str();
}

}  // namespace mir2
