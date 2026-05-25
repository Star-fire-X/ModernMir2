#include "services/persistence_service.hpp"

namespace mir2 {

namespace {

std::string persist_request_kind_name(PersistRequestKind kind) {
  switch (kind) {
    case PersistRequestKind::ensure_schema:
      return "ensure_schema";
    case PersistRequestKind::load_account:
      return "load_account";
    case PersistRequestKind::authenticate_account:
      return "authenticate_account";
    case PersistRequestKind::load_castle_dialog_context:
      return "load_castle_dialog_context";
    case PersistRequestKind::load_guild_castle_snapshot:
      return "load_guild_castle_snapshot";
    case PersistRequestKind::save_guild_payload:
      return "save_guild_payload";
    case PersistRequestKind::save_guild_state:
      return "save_guild_state";
    case PersistRequestKind::delete_guild:
      return "delete_guild";
    case PersistRequestKind::save_castle_state:
      return "save_castle_state";
    case PersistRequestKind::create_account:
      return "create_account";
    case PersistRequestKind::update_account:
      return "update_account";
    case PersistRequestKind::change_password:
      return "change_password";
    case PersistRequestKind::load_character:
      return "load_character";
    case PersistRequestKind::load_character_by_name:
      return "load_character_by_name";
    case PersistRequestKind::list_characters:
      return "list_characters";
    case PersistRequestKind::create_character:
      return "create_character";
    case PersistRequestKind::delete_character:
      return "delete_character";
    case PersistRequestKind::save_character:
      return "save_character";
    case PersistRequestKind::load_merchant_states:
      return "load_merchant_states";
    case PersistRequestKind::save_merchant_state:
      return "save_merchant_state";
    case PersistRequestKind::record_audit:
      return "record_audit";
    case PersistRequestKind::seed_runtime:
      return "seed_runtime";
  }
  return "unknown";
}

}  // namespace

void PersistenceService::start(HostContext& context) {
  context_ = &context;
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
  repository_ = std::make_unique<Repository>(context.root_dir / context.config.runtime.data_dir / "mir2.sqlite");
  repository_->ensure_schema(context.root_dir / "schema" / "mir2.sql");
  repository_->seed_runtime();
  running_.store(true, std::memory_order_relaxed);
  worker_ = std::thread([this] { run(); });
}

void PersistenceService::stop() { running_.store(false, std::memory_order_relaxed); }

void PersistenceService::join() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::unordered_map<std::string, std::string> PersistenceService::snapshot() const {
  return {{"running", running_.load(std::memory_order_relaxed) ? "true" : "false"},
          {"handled_requests", std::to_string(handled_requests_)},
          {"last_request_kind", persist_request_kind_name(last_request_kind_)},
          {"last_request_reply_to", last_request_reply_to_},
          {"last_request_id", last_request_id_}};
}

void PersistenceService::run() {
  while (running_.load(std::memory_order_relaxed) || endpoint_->queue->size() > 0) {
    const auto timeout = running_.load(std::memory_order_relaxed) ? std::chrono::milliseconds(100)
                                                                 : std::chrono::milliseconds(1);
    auto message = endpoint_->queue->wait_pop_for(timeout);
    if (!message.has_value()) {
      continue;
    }
    if (auto request = std::get_if<PersistRequest>(&*message)) {
      handle_request(*request);
    }
  }
}

void PersistenceService::handle_request(const PersistRequest& request) {
  ++handled_requests_;
  last_request_kind_ = request.kind;
  last_request_reply_to_ = request.reply_to;
  last_request_id_ = request.request_id;
  try {
    const auto post_result = [&](PersistResult result) {
      if (!request.reply_to.empty()) {
        context_->bus->post(request.reply_to, std::move(result));
      }
    };
    const auto push_guild_castle_snapshot = [&](const GuildCastleSnapshot& guild_castle_snapshot) {
      PersistResult refresh_result;
      refresh_result.kind = PersistResultKind::guild_castle_snapshot_loaded;
      refresh_result.guild_castle_snapshot = guild_castle_snapshot;
      refresh_result.castle_dialog_context = guild_castle_snapshot.castle_dialog;
      context_->bus->post("world_service", refresh_result);
      if (!request.reply_to.empty() && request.reply_to != "world_service") {
        PersistResult echoed_result;
        echoed_result.kind = PersistResultKind::guild_castle_snapshot_loaded;
        echoed_result.reply_to = request.reply_to;
        echoed_result.request_id = request.request_id;
        echoed_result.guild_castle_snapshot = guild_castle_snapshot;
        echoed_result.castle_dialog_context = guild_castle_snapshot.castle_dialog;
        context_->bus->post(request.reply_to, std::move(echoed_result));
      }
    };
    switch (request.kind) {
      case PersistRequestKind::ensure_schema:
        break;
      case PersistRequestKind::seed_runtime:
        repository_->seed_runtime();
        break;
      case PersistRequestKind::load_account: {
        if (const auto account = repository_->load_account(request.account_id); account.has_value()) {
          PersistResult result;
          result.kind = PersistResultKind::account_loaded;
          result.reply_to = request.reply_to;
          result.account_id = request.account_id;
          result.account = *account;
          result.request_id = request.request_id;
          result.result_code = 1;
          post_result(std::move(result));
        } else {
          PersistResult result;
          result.kind = PersistResultKind::account_loaded;
          result.reply_to = request.reply_to;
          result.account_id = request.account_id;
          result.request_id = request.request_id;
          result.result_code = 0;
          post_result(std::move(result));
        }
        break;
      }
      case PersistRequestKind::authenticate_account: {
        const auto auth =
            repository_->authenticate_account(request.account_id, request.password, request.timestamp_ms);
        PersistResult result;
        result.kind = PersistResultKind::account_authenticated;
        result.reply_to = request.reply_to;
        result.account_id = request.account_id;
        result.request_id = request.request_id;
        result.result_code = auth.status_code;
        if (auth.account.has_value()) {
          result.account = *auth.account;
        }
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::load_castle_dialog_context: {
        PersistResult result;
        result.kind = PersistResultKind::castle_dialog_context_loaded;
        result.reply_to = request.reply_to;
        result.request_id = request.request_id;
        result.castle_dialog_context = repository_->load_castle_dialog_context();
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::load_guild_castle_snapshot: {
        PersistResult result;
        result.kind = PersistResultKind::guild_castle_snapshot_loaded;
        result.reply_to = request.reply_to;
        result.request_id = request.request_id;
        result.guild_castle_snapshot = repository_->load_guild_castle_snapshot();
        result.castle_dialog_context = result.guild_castle_snapshot.castle_dialog;
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::save_guild_payload: {
        repository_->save_guild_payload(request.guild_name, request.payload_json);
        push_guild_castle_snapshot(repository_->load_guild_castle_snapshot());
        break;
      }
      case PersistRequestKind::save_guild_state: {
        repository_->save_guild_state(request.guild_state);
        push_guild_castle_snapshot(repository_->load_guild_castle_snapshot());
        break;
      }
      case PersistRequestKind::delete_guild: {
        repository_->delete_guild(request.guild_name);
        push_guild_castle_snapshot(repository_->load_guild_castle_snapshot());
        break;
      }
      case PersistRequestKind::save_castle_state: {
        repository_->save_castle_state(request.castle_name, request.payload_json);
        push_guild_castle_snapshot(repository_->load_guild_castle_snapshot());
        break;
      }
      case PersistRequestKind::create_account: {
        PersistResult result;
        result.kind = PersistResultKind::account_created;
        result.reply_to = request.reply_to;
        result.account_id = request.account.account_id;
        result.account = request.account;
        result.request_id = request.request_id;
        result.result_code = repository_->create_account(request.account) ? 1 : 0;
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::update_account: {
        PersistResult result;
        result.kind = PersistResultKind::account_updated;
        result.reply_to = request.reply_to;
        result.account_id = request.account.account_id;
        result.account = request.account;
        result.request_id = request.request_id;
        result.result_code = repository_->update_account(request.account) ? 1 : 0;
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::change_password: {
        PersistResult result;
        result.kind = PersistResultKind::password_changed;
        result.reply_to = request.reply_to;
        result.account_id = request.account_id;
        result.request_id = request.request_id;
        result.result_code = repository_->change_password(request.account_id, request.password,
                                                          request.new_password, request.timestamp_ms);
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::load_character: {
        auto record = repository_->load_character(request.account_id, request.character_name);
        if (record.has_value()) {
          PersistResult result;
          result.kind = PersistResultKind::character_loaded;
          result.reply_to = request.reply_to;
          result.account_id = request.account_id;
          result.character_name = request.character_name;
          result.character = *record;
          result.request_id = request.request_id;
          post_result(std::move(result));
        }
        break;
      }
      case PersistRequestKind::load_character_by_name: {
        const auto record = repository_->load_character_by_name(request.character_name);
        PersistResult result;
        result.kind = PersistResultKind::character_loaded;
        result.reply_to = request.reply_to;
        result.request_id = request.request_id;
        result.character_name = request.character_name;
        result.result_code = record.has_value() ? 1 : 0;
        if (record.has_value()) {
          result.account_id = record->account_id;
          result.character_name = record->character_name;
          result.character = *record;
        }
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::list_characters:
        {
          PersistResult result;
          result.kind = PersistResultKind::characters_listed;
          result.reply_to = request.reply_to;
          result.account_id = request.account_id;
          result.characters = repository_->list_characters(request.account_id);
          result.request_id = request.request_id;
          post_result(std::move(result));
        }
        break;
      case PersistRequestKind::create_character: {
        const auto created = repository_->create_character(request.character);
        PersistResult result;
        result.kind = created ? PersistResultKind::character_created : PersistResultKind::error;
        result.reply_to = request.reply_to;
        result.account_id = request.character.account_id;
        result.character_name = request.character.character_name;
        result.character = request.character;
        result.error = created ? std::string{} : "character_create_failed";
        result.request_id = request.request_id;
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::delete_character: {
        const auto deleted = repository_->delete_character(request.account_id, request.character_name);
        PersistResult result;
        result.kind = deleted ? PersistResultKind::character_deleted : PersistResultKind::error;
        result.reply_to = request.reply_to;
        result.account_id = request.account_id;
        result.character_name = request.character_name;
        result.error = deleted ? std::string{} : "character_delete_failed";
        result.request_id = request.request_id;
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::save_character:
        {
          const auto saved = repository_->save_character(request.character);
          PersistResult result;
          result.kind = PersistResultKind::character_saved;
          result.reply_to = request.reply_to;
          result.account_id = request.character.account_id;
          result.character_name = request.character.character_name;
          result.character = request.character;
          result.request_id = request.request_id;
          result.result_code = saved ? 1 : 0;
          post_result(std::move(result));
        }
        break;
      case PersistRequestKind::load_merchant_states: {
        PersistResult result;
        result.kind = PersistResultKind::merchant_states_loaded;
        result.reply_to = request.reply_to;
        result.request_id = request.request_id;
        result.merchant_states = repository_->load_merchant_states();
        result.result_code = 1;
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::save_merchant_state: {
        repository_->save_merchant_state(request.merchant_state);
        PersistResult result;
        result.kind = PersistResultKind::merchant_state_saved;
        result.reply_to = request.reply_to;
        result.request_id = request.request_id;
        result.result_code = 1;
        post_result(std::move(result));
        break;
      }
      case PersistRequestKind::record_audit:
        repository_->record_audit(AuditEvent{"persist.audit", request.text, request.account_id});
        break;
    }
  } catch (const std::exception& ex) {
    PersistResult result;
    result.kind = PersistResultKind::error;
    result.reply_to = request.reply_to;
    result.account_id = request.account_id;
    result.character_name = request.character_name;
    result.error = ex.what();
    result.request_id = request.request_id;
    if (!request.reply_to.empty()) {
      context_->bus->post(request.reply_to, std::move(result));
    }
  }
}

}  // namespace mir2
