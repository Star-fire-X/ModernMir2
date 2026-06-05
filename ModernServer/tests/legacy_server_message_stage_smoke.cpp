#include <cassert>
#include <string>

#include "services/world_service.hpp"

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "ServerMessageStageMap", {}, 0, 0, 10, 10});

  mir2::HostContext context;
  context.config = config;

  mir2::WorldService world;
  world.attach_context_for_test(context);
  world.initialize_runtime_for_test(config);

  mir2::PersistResult guild;
  guild.kind = mir2::PersistResultKind::guild_castle_snapshot_loaded;
  guild.guild_castle_snapshot.guilds.push_back(
      mir2::GuildState{"DeferredGuild", "DeferredLord", {"DeferredLord"}});

  mir2::PersistResult castle;
  castle.kind = mir2::PersistResultKind::castle_dialog_context_loaded;
  castle.castle_dialog_context.castle_name = "Sabuk";
  castle.castle_dialog_context.owner_guild = "DeferredGuild";
  castle.castle_dialog_context.lord = "DeferredLord";

  mir2::PersistResult merchants;
  merchants.kind = mir2::PersistResultKind::merchant_states_loaded;

  mir2::PersistResult offline_error;
  offline_error.kind = mir2::PersistResultKind::error;
  offline_error.request_id = "guild_offline\n0\n1\napprove\nDeferredGuild\nTarget\n\n";

  mir2::WorldIngressBatch batch;
  batch.push(guild, 10);
  batch.push(castle, 11);
  batch.push(merchants, 12);
  batch.push(offline_error, 13);
  batch.mark_frame(5);

  const auto decoded = world.process_ingress_batch_for_test(batch);
  assert(batch.empty());
  assert(decoded.audit_events.empty());
  auto snapshot = world.snapshot();
  assert(snapshot.at("guild_count") == "0");
  assert(snapshot.at("offline_guild_errors") == "0");

  const auto applied = world.run_server_message_stage_for_test(2000);
  assert(applied.audit_events.empty());
  snapshot = world.snapshot();
  assert(snapshot.at("guild_count") == "1");
  assert(snapshot.at("castle_owner_guild") == "DeferredGuild");
  assert(snapshot.at("offline_guild_errors") == "1");

  const auto empty = world.run_server_message_stage_for_test(2001);
  assert(empty.audit_events.empty());
  assert(empty.legacy_traces.empty());
  return 0;
}
