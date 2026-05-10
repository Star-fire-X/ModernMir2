#include "services/auth_service.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "util/string_utils.hpp"

namespace mir2 {

namespace {

constexpr std::size_t kMaxCharacterSlots = 2;
constexpr std::int64_t kAccountCommandThrottleMs = 5 * 1000;

LegacyPacket make_response_packet(std::uint64_t session_id, std::uint16_t ident,
                                  std::int32_t recog = 0, std::uint16_t param = 0,
                                  std::uint16_t tag = 0, std::uint16_t series = 0,
                                  const std::string& body = {}) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(ident, recog, param, tag, series), body);
}

void post_gateway_packet(HostContext& context, std::uint64_t session_id, LegacyPacket packet) {
  context.bus->post("login_gateway",
                    SessionEvent{SessionEventKind::send_packet, "login_gateway", session_id, {},
                                 std::move(packet), {}});
}

void audit(HostContext& context, std::string category, std::string message, std::string session_key) {
  context.bus->post("log_service",
                    AuditEvent{std::move(category), std::move(message), std::move(session_key)});
}

std::vector<std::string> split_fields(std::string_view text, char delimiter) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find(delimiter, start);
    if (end == std::string_view::npos) {
      fields.emplace_back(text.substr(start));
      break;
    }
    fields.emplace_back(text.substr(start, end - start));
    start = end + 1;
  }
  return fields;
}

std::optional<std::int32_t> parse_i32(std::string_view text) {
  std::int32_t value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool is_valid_character_name(const std::string& name) {
  static constexpr std::string_view kInvalidChars = " /@?'\"\\.,:;`~!#$%^&*()-_+=|[]{}";
  if (name.size() < 3 || name.size() > 14) {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
           return std::isalnum(ch) != 0;
         }) && name.find_first_of(kInvalidChars) == std::string::npos;
}

bool is_valid_account_id(std::string_view account_id) {
  if (account_id.empty()) {
    return false;
  }

  for (std::size_t index = 0; index < account_id.size(); ++index) {
    const auto ch = static_cast<unsigned char>(account_id[index]);
    if (ch >= 48 && ch <= 122) {
      continue;
    }
    if (ch >= 0xB0 && ch <= 0xC8 && index + 1 < account_id.size()) {
      const auto next = static_cast<unsigned char>(account_id[index + 1]);
      if (next >= 0xA1 && next <= 0xFE) {
        ++index;
        continue;
      }
    }
    return false;
  }
  return true;
}

bool should_throttle(std::int64_t& last_command_at_ms, std::int64_t current_ms) {
  if (last_command_at_ms != 0 && current_ms - last_command_at_ms <= kAccountCommandThrottleMs) {
    return true;
  }
  last_command_at_ms = current_ms;
  return false;
}

bool requires_account_update(const AccountRecord& account) {
  return account.user_name.empty() || account.quiz2.empty();
}

template <typename T>
std::size_t encoded_buffer_size() {
  static const auto size = [] {
    const T value{};
    return legacy_encode_buffer(&value, sizeof(T)).size();
  }();
  return size;
}

bool decode_user_entry_body(std::string_view body, LegacyUserEntryInfo& info,
                            LegacyUserEntryAddInfo& add_info) {
  const auto info_size = encoded_buffer_size<LegacyUserEntryInfo>();
  if (body.size() < info_size) {
    return false;
  }
  const auto info_body = body.substr(0, info_size);
  const auto add_body = body.substr(info_size);
  return legacy_decode_buffer(info_body, &info, sizeof(info)) &&
         legacy_decode_buffer(add_body, &add_info, sizeof(add_info));
}

AccountRecord make_account_record(const LegacyUserEntryInfo& info, const LegacyUserEntryAddInfo& add_info) {
  AccountRecord account;
  account.account_id = to_string(info.login_id);
  account.password = to_string(info.password);
  account.user_name = to_string(info.user_name);
  account.display_name = account.user_name.empty() ? account.account_id : account.user_name;
  account.ss_no = to_string(info.ss_no);
  account.phone = to_string(info.phone);
  account.quiz = to_string(info.quiz);
  account.answer = to_string(info.answer);
  account.email = to_string(info.email);
  account.quiz2 = to_string(add_info.quiz2);
  account.answer2 = to_string(add_info.answer2);
  account.birthday = to_string(add_info.birthday);
  account.mobile_phone = to_string(add_info.mobile_phone);
  account.memo1 = to_string(add_info.memo1);
  account.memo2 = to_string(add_info.memo2);
  return account;
}

LegacyUserEntryInfo make_user_entry_info(const AccountRecord& account) {
  LegacyUserEntryInfo info;
  set_short_string(info.login_id, account.account_id);
  set_short_string(info.password, account.password);
  set_short_string(info.user_name, account.user_name);
  set_short_string(info.ss_no, account.ss_no);
  set_short_string(info.phone, account.phone);
  set_short_string(info.quiz, account.quiz);
  set_short_string(info.answer, account.answer);
  set_short_string(info.email, account.email);
  return info;
}

CharacterRecord make_new_character(const std::string& account_id, const std::string& character_name,
                                   std::uint8_t job, std::uint8_t sex, std::uint8_t hair) {
  CharacterRecord record;
  record.account_id = account_id;
  record.character_name = character_name;
  record.map_id = "0";
  record.x = 330;
  record.y = 270;
  record.dir = 0;
  record.light = 0;
  record.job = job;
  record.sex = sex;
  record.hair = hair;
  record.gold = 2000;
  record.feature = 0;
  record.status = 0;
  record.ability.level = 1;
  record.ability.hp = 15;
  record.ability.mp = 15;
  record.ability.max_hp = 15;
  record.ability.max_mp = 15;
  record.ability.max_exp = 100;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  return record;
}

std::string encode_select_server_body(const HostContext& context, std::int32_t certification) {
  return legacy_encode_string(context.config.ports.game_gateway.address + "/" +
                              std::to_string(context.config.ports.game_gateway.port) + "/" +
                              std::to_string(certification));
}

std::string encode_start_play_body(const HostContext& context) {
  return legacy_encode_string(context.config.ports.game_gateway.address + "/" +
                              std::to_string(context.config.ports.game_gateway.port));
}

std::string encode_character_list_body(const std::vector<CharacterRecord>& characters,
                                       std::string selected_character) {
  if (selected_character.empty() && !characters.empty()) {
    selected_character = characters.front().character_name;
  }

  std::string plain;
  for (std::size_t index = 0; index < kMaxCharacterSlots; ++index) {
    if (index < characters.size()) {
      auto name = characters[index].character_name;
      if (!selected_character.empty() && name == selected_character) {
        name.insert(name.begin(), '*');
      }
      plain += name + "/" + std::to_string(characters[index].job) + "/" +
               std::to_string(characters[index].hair) + "/" +
               std::to_string(characters[index].ability.level) + "/" +
               std::to_string(characters[index].sex) + "/";
    } else {
      plain += "/////";
    }
  }
  return legacy_encode_string(plain);
}

}  // namespace

void AuthService::start(HostContext& context) {
  context_ = &context;
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
  running_.store(true, std::memory_order_relaxed);
  worker_ = std::thread([this] { run(); });
}

void AuthService::stop() { running_.store(false, std::memory_order_relaxed); }

void AuthService::join() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::unordered_map<std::string, std::string> AuthService::snapshot() const {
  return {{"running", running_.load(std::memory_order_relaxed) ? "true" : "false"},
          {"pending_requests", std::to_string(pending_requests_.size())},
          {"session_states", std::to_string(session_states_.size())},
          {"admissions", std::to_string(admissions_.size())}};
}

std::string AuthService::make_request_id() { return "auth:" + std::to_string(next_request_id_++); }

void AuthService::run() {
  while (running_.load(std::memory_order_relaxed)) {
    auto message = endpoint_->queue->wait_pop_for(std::chrono::milliseconds(100));
    if (!message.has_value()) {
      continue;
    }
    if (auto event = std::get_if<SessionEvent>(&*message)) {
      handle_session_event(*event);
    } else if (auto command = std::get_if<LogicCommand>(&*message)) {
      handle_logic_command(*command);
    } else if (auto result = std::get_if<PersistResult>(&*message)) {
      handle_persist_result(*result);
    }
  }
}

void AuthService::handle_session_event(const SessionEvent& event) {
  if (context_ == nullptr) {
    return;
  }

  if (event.kind == SessionEventKind::disconnected) {
    if (const auto session = session_states_.find(event.session_id); session != session_states_.end()) {
      if (session->second.certification > 0) {
        const auto admission = admissions_.find(session->second.certification);
        if (admission != admissions_.end() && admission->second.selected_character.empty()) {
          admissions_.erase(admission);
        }
      }
      session_states_.erase(session);
    }
    for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
      if (it->second.session_id == event.session_id) {
        it = pending_requests_.erase(it);
      } else {
        ++it;
      }
    }
    return;
  }

  if (event.kind != SessionEventKind::packet_received) {
    return;
  }

  const auto decoded = decode_legacy_game_packet(event.packet);
  if (!decoded.has_value()) {
    return;
  }

  switch (decoded->message.ident) {
    case kCmIdPassword: {
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto password = fields.size() > 1 ? fields[1] : "";
      if (account_id.empty() || password.empty()) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmPasswdFail, -4));
        audit(*context_, "auth.login_fail", account_id, std::to_string(event.session_id));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::authenticate_account;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.client_version = decoded->message.recog;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::authenticate_account;
      request.reply_to = name();
      request.account_id = account_id;
      request.password = password;
      request.request_id = request_id;
      request.timestamp_ms = now_ms();
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmAddNewUser: {
      auto& session = session_states_[event.session_id];
      const auto current_ms = now_ms();
      if (should_throttle(session.last_command_at_ms, current_ms)) {
        return;
      }

      LegacyUserEntryInfo info{};
      LegacyUserEntryAddInfo add_info{};
      if (!decode_user_entry_body(decoded->body, info, add_info)) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmNewIdFail, 0));
        return;
      }

      auto account = make_account_record(info, add_info);
      if (!is_valid_account_id(account.account_id)) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmNewIdFail, 0));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::create_account;
      pending.session_id = event.session_id;
      pending.account_id = account.account_id;
      pending.account = account;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::create_account;
      request.reply_to = name();
      request.account_id = account.account_id;
      request.account = std::move(account);
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmUpdateUser: {
      auto& session = session_states_[event.session_id];
      const auto current_ms = now_ms();
      if (should_throttle(session.last_command_at_ms, current_ms)) {
        return;
      }

      LegacyUserEntryInfo info{};
      LegacyUserEntryAddInfo add_info{};
      if (!decode_user_entry_body(decoded->body, info, add_info)) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmUpdateIdFail, -1));
        return;
      }

      auto account = make_account_record(info, add_info);
      if (session.account_id.empty() || session.account_id != account.account_id ||
          !is_valid_account_id(account.account_id)) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmUpdateIdFail, -1));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::update_account;
      pending.session_id = event.session_id;
      pending.account_id = account.account_id;
      pending.account = account;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::update_account;
      request.reply_to = name();
      request.account_id = account.account_id;
      request.account = std::move(account);
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmChangePassword: {
      auto& session = session_states_[event.session_id];
      if (!session.account_id.empty()) {
        return;
      }
      const auto current_ms = now_ms();
      if (should_throttle(session.last_command_at_ms, current_ms)) {
        return;
      }

      const auto fields = split_fields(legacy_decode_string(decoded->body), '\t');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto password = fields.size() > 1 ? fields[1] : "";
      const auto new_password = fields.size() > 2 ? fields[2] : "";

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::change_password;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::change_password;
      request.reply_to = name();
      request.account_id = account_id;
      request.password = password;
      request.new_password = new_password;
      request.request_id = request_id;
      request.timestamp_ms = current_ms;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmSelectServer: {
      auto session = session_states_.find(event.session_id);
      if (session == session_states_.end() || session->second.account_id.empty() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::select_server) ||
          admissions_.find(session->second.certification) == admissions_.end()) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmPasswdFail, -4));
        return;
      }

      session->second.stage =
          advance(session->second.stage, CanonicalLoginTransition::select_server);
      if (auto admission = admissions_.find(session->second.certification);
          admission != admissions_.end()) {
        admission->second.stage = session->second.stage;
      }

      post_gateway_packet(
          *context_, event.session_id,
          make_response_packet(event.session_id, kSmSelectServerOk, session->second.certification, 0,
                               0, 0,
                               encode_select_server_body(*context_, session->second.certification)));
      return;
    }

    case kCmQueryChr: {
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto certification = fields.size() > 1 ? parse_i32(fields[1]) : std::nullopt;

      const auto admission =
          certification.has_value() ? admissions_.find(*certification) : admissions_.end();
      const auto session = session_states_.find(event.session_id);
      if (account_id.empty() || !certification.has_value() || session == session_states_.end() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::query_characters) ||
          admission == admissions_.end() ||
          admission->second.account_id != account_id) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmQueryChrFail, 0, 0, 0, 1));
        audit(*context_, "auth.query_chr_fail", account_id, std::to_string(event.session_id));
        return;
      }

      auto& session_state = session->second;
      session_state.account_id = account_id;
      session_state.certification = *certification;
      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::query_characters;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.certification = *certification;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::list_characters;
      request.reply_to = name();
      request.account_id = account_id;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmNewChr: {
      const auto session = session_states_.find(event.session_id);
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto character_name = fields.size() > 1 ? fields[1] : "";
      const auto hair = fields.size() > 2 ? parse_i32(fields[2]) : std::nullopt;
      const auto job = fields.size() > 3 ? parse_i32(fields[3]) : std::nullopt;
      const auto sex = fields.size() > 4 ? parse_i32(fields[4]) : std::nullopt;

      if (session == session_states_.end() || session->second.account_id != account_id ||
          !can_accept(session->second.stage, CanonicalLoginRequest::create_character) ||
          admissions_.find(session->second.certification) == admissions_.end() ||
          !hair.has_value() || !job.has_value() || !sex.has_value() ||
          !is_valid_character_name(character_name)) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmNewChrFail, 0));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::create_precheck;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.character_name = character_name;
      pending.certification = session->second.certification;
      pending.character =
          make_new_character(account_id, character_name, static_cast<std::uint8_t>(*job),
                             static_cast<std::uint8_t>(*sex), static_cast<std::uint8_t>(*hair));
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::list_characters;
      request.reply_to = name();
      request.account_id = account_id;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmDelChr: {
      const auto session = session_states_.find(event.session_id);
      const auto character_name = legacy_decode_string(decoded->body);
      if (session == session_states_.end() || character_name.empty() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::delete_character) ||
          admissions_.find(session->second.certification) == admissions_.end()) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmDelChrFail, 0));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::delete_character;
      pending.session_id = event.session_id;
      pending.account_id = session->second.account_id;
      pending.character_name = character_name;
      pending.certification = session->second.certification;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::delete_character;
      request.reply_to = name();
      request.account_id = session->second.account_id;
      request.character_name = character_name;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmSelChr: {
      const auto session = session_states_.find(event.session_id);
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto character_name = fields.size() > 1 ? fields[1] : "";

      if (session == session_states_.end() || session->second.account_id != account_id ||
          character_name.empty() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::select_character) ||
          admissions_.find(session->second.certification) == admissions_.end()) {
        post_gateway_packet(*context_, event.session_id,
                            make_response_packet(event.session_id, kSmStartFail, 0));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::select_character;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.character_name = character_name;
      pending.certification = session->second.certification;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::list_characters;
      request.reply_to = name();
      request.account_id = account_id;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    default:
      return;
  }
}

void AuthService::handle_logic_command(const LogicCommand& command) {
  if (command.kind != LogicCommandKind::revoke_authentication) {
    return;
  }

  if (command.certification > 0) {
    admissions_.erase(command.certification);
  } else if (!command.account_id.empty()) {
    for (auto it = admissions_.begin(); it != admissions_.end();) {
      if (it->second.account_id == command.account_id) {
        it = admissions_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void AuthService::handle_persist_result(const PersistResult& result) {
  if (context_ == nullptr || result.request_id.empty()) {
    return;
  }

  const auto pending_it = pending_requests_.find(result.request_id);
  if (pending_it == pending_requests_.end()) {
    return;
  }

  auto pending = pending_it->second;
  switch (result.kind) {
    case PersistResultKind::account_loaded:
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::account_authenticated: {
      if (pending.kind != PendingAuthRequestKind::authenticate_account) {
        pending_requests_.erase(pending_it);
        return;
      }

      if (result.result_code == 1 && result.account.account_id == pending.account_id) {
        const auto duplicate =
            std::any_of(session_states_.begin(), session_states_.end(), [&](const auto& item) {
              return item.first != pending.session_id && item.second.account_id == pending.account_id &&
                     !item.second.account_id.empty();
            }) ||
            std::any_of(admissions_.begin(), admissions_.end(), [&](const auto& item) {
              return item.second.account_id == pending.account_id;
            });
        if (duplicate) {
          std::unordered_map<std::int32_t, LoginAdmission> revoked_admissions;
          std::vector<std::uint64_t> revoked_login_sessions;

          for (auto it = session_states_.begin(); it != session_states_.end();) {
            if (it->first != pending.session_id && it->second.account_id == pending.account_id &&
                !it->second.account_id.empty()) {
              revoked_login_sessions.push_back(it->first);
              if (it->second.certification > 0) {
                if (const auto admission = admissions_.find(it->second.certification);
                    admission != admissions_.end()) {
                  revoked_admissions[it->second.certification] = admission->second;
                } else {
                  revoked_admissions[it->second.certification] =
                      LoginAdmission{pending.account_id, {}, it->second.certification,
                                     CanonicalLoginStage::authenticated};
                }
              }
              it = session_states_.erase(it);
            } else {
              ++it;
            }
          }

          for (const auto& [certification, admission] : admissions_) {
            if (admission.account_id == pending.account_id) {
              revoked_admissions[certification] = admission;
            }
          }
          for (const auto& [certification, _] : revoked_admissions) {
            admissions_.erase(certification);
          }

          for (const auto session_id : revoked_login_sessions) {
            context_->bus->post(
                "login_gateway",
                SessionEvent{SessionEventKind::force_disconnect, "login_gateway", session_id, {}, {},
                             "duplicate_login"});
          }
          for (const auto& [_, admission] : revoked_admissions) {
            LogicCommand revoke;
            revoke.kind = LogicCommandKind::revoke_authentication;
            revoke.account_id = admission.account_id;
            revoke.character_name = admission.selected_character;
            revoke.certification = admission.certification;
            context_->bus->post("world_service", std::move(revoke));
          }

          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmPasswdFail, -3));
          audit(*context_, "auth.login_duplicate", pending.account_id,
                std::to_string(pending.session_id));
          pending_requests_.erase(pending_it);
          return;
        }

        if (requires_account_update(result.account)) {
          const auto info = make_user_entry_info(result.account);
          post_gateway_packet(
              *context_, pending.session_id,
              make_response_packet(pending.session_id, kSmNeedUpdateAccount, 0, 0, 0, 0,
                                   legacy_encode_buffer(&info, sizeof(info))));
        }

        const auto certification = std::max(next_certification_.fetch_add(1), 2);
        auto& session = session_states_[pending.session_id];
        session.account_id = pending.account_id;
        session.certification = certification;
        session.client_version = pending.client_version;
        session.stage = advance(session.stage, CanonicalLoginTransition::authenticate);
        admissions_[certification] =
            LoginAdmission{pending.account_id, {}, certification, session.stage};

        post_gateway_packet(
            *context_, pending.session_id,
            make_response_packet(pending.session_id, kSmPassOkSelectServer, 0, 0, 0, 0));
        audit(*context_, "auth.login_ok", pending.account_id, std::to_string(pending.session_id));
      } else {
        post_gateway_packet(*context_, pending.session_id,
                            make_response_packet(pending.session_id, kSmPasswdFail,
                                                 result.result_code == 0 ? -4 : result.result_code));
        audit(*context_, "auth.login_fail", pending.account_id, std::to_string(pending.session_id));
      }
      pending_requests_.erase(pending_it);
      return;
    }

    case PersistResultKind::account_created:
      post_gateway_packet(*context_, pending.session_id,
                          make_response_packet(pending.session_id,
                                               result.result_code == 1 ? kSmNewIdSuccess : kSmNewIdFail,
                                               result.result_code == 1 ? 0 : result.result_code));
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::account_updated:
      post_gateway_packet(
          *context_, pending.session_id,
          make_response_packet(pending.session_id,
                               result.result_code == 1 ? kSmUpdateIdSuccess : kSmUpdateIdFail,
                               result.result_code == 1 ? 0 : result.result_code));
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::password_changed:
      post_gateway_packet(
          *context_, pending.session_id,
          make_response_packet(
              pending.session_id,
              result.result_code == 1 ? kSmChgPasswdSuccess : kSmChgPasswdFail,
              result.result_code == 1 ? 0 : result.result_code));
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::characters_listed:
      switch (pending.kind) {
        case PendingAuthRequestKind::query_characters: {
          const auto selected_it = last_selected_character_.find(pending.account_id);
          const auto selected =
              selected_it != last_selected_character_.end() ? selected_it->second : std::string{};
          post_gateway_packet(
              *context_, pending.session_id,
              make_response_packet(pending.session_id, kSmQueryChr,
                                   static_cast<std::int32_t>(result.characters.size()), 0, 0, 1,
                                   encode_character_list_body(result.characters, selected)));
          audit(*context_, "auth.query_chr_ok", pending.account_id,
                std::to_string(pending.session_id));
          pending_requests_.erase(pending_it);
          return;
        }

        case PendingAuthRequestKind::create_precheck: {
          const auto exists = std::any_of(result.characters.begin(), result.characters.end(),
                                          [&](const CharacterRecord& character) {
                                            return character.character_name == pending.character_name;
                                          });
          if (exists) {
            post_gateway_packet(*context_, pending.session_id,
                                make_response_packet(pending.session_id, kSmNewChrFail, 2));
            pending_requests_.erase(pending_it);
            return;
          }
          if (result.characters.size() >= kMaxCharacterSlots) {
            post_gateway_packet(*context_, pending.session_id,
                                make_response_packet(pending.session_id, kSmNewChrFail, 3));
            pending_requests_.erase(pending_it);
            return;
          }

          pending_requests_[result.request_id].kind = PendingAuthRequestKind::create_commit;
          PersistRequest request;
          request.kind = PersistRequestKind::create_character;
          request.reply_to = name();
          request.account_id = pending.character.account_id;
          request.character_name = pending.character.character_name;
          request.character = pending.character;
          request.request_id = result.request_id;
          context_->bus->post("persistence_service", std::move(request));
          return;
        }

        case PendingAuthRequestKind::select_character: {
          const auto found = std::find_if(result.characters.begin(), result.characters.end(),
                                          [&](const CharacterRecord& character) {
                                            return character.character_name == pending.character_name;
                                          });
          if (found == result.characters.end()) {
            post_gateway_packet(*context_, pending.session_id,
                                make_response_packet(pending.session_id, kSmStartFail, 0));
            pending_requests_.erase(pending_it);
            return;
          }

          admissions_[pending.certification] =
              LoginAdmission{pending.account_id, pending.character_name, pending.certification,
                             CanonicalLoginStage::character_selected};
          if (auto session = session_states_.find(pending.session_id);
              session != session_states_.end()) {
            session->second.stage =
                advance(session->second.stage, CanonicalLoginTransition::select_character);
          }
          last_selected_character_[pending.account_id] = pending.character_name;

          LogicCommand admission;
          admission.kind = LogicCommandKind::authenticate;
          admission.account_id = pending.account_id;
          admission.character_name = pending.character_name;
          admission.certification = pending.certification;
          context_->bus->post("world_service", std::move(admission));

          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmStartPlay, 0, 0, 0, 0,
                                                   encode_start_play_body(*context_)));
          audit(*context_, "auth.start_play", pending.account_id + ":" + pending.character_name,
                std::to_string(pending.session_id));
          pending_requests_.erase(pending_it);
          return;
        }

        case PendingAuthRequestKind::authenticate_account:
        case PendingAuthRequestKind::create_account:
        case PendingAuthRequestKind::update_account:
        case PendingAuthRequestKind::change_password:
        case PendingAuthRequestKind::create_commit:
        case PendingAuthRequestKind::delete_character:
          break;
      }
      break;

    case PersistResultKind::character_created:
      post_gateway_packet(*context_, pending.session_id,
                          make_response_packet(pending.session_id, kSmNewChrSuccess, 1));
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::character_deleted:
      post_gateway_packet(*context_, pending.session_id,
                          make_response_packet(pending.session_id, kSmDelChrSuccess, 1));
      if (const auto selected = last_selected_character_.find(pending.account_id);
          selected != last_selected_character_.end() &&
          selected->second == pending.character_name) {
        last_selected_character_.erase(pending.account_id);
      }
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::error:
      switch (pending.kind) {
        case PendingAuthRequestKind::authenticate_account:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmPasswdFail, -4));
          break;
        case PendingAuthRequestKind::create_account:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmNewIdFail, 0));
          break;
        case PendingAuthRequestKind::update_account:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmUpdateIdFail, -1));
          break;
        case PendingAuthRequestKind::change_password:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmChgPasswdFail, 0));
          break;
        case PendingAuthRequestKind::query_characters:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmQueryChrFail, 0, 0, 0, 1));
          break;
        case PendingAuthRequestKind::create_precheck:
        case PendingAuthRequestKind::create_commit:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmNewChrFail, 4));
          break;
        case PendingAuthRequestKind::delete_character:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmDelChrFail, 0));
          break;
        case PendingAuthRequestKind::select_character:
          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmStartFail, 0));
          break;
      }
      pending_requests_.erase(pending_it);
      audit(*context_, "auth.persist_error", result.error, std::to_string(pending.session_id));
      return;

    case PersistResultKind::schema_ready:
    case PersistResultKind::character_loaded:
    case PersistResultKind::character_saved:
    case PersistResultKind::audit_recorded:
    case PersistResultKind::seeded:
      return;
  }
}

}  // namespace mir2
