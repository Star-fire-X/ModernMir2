#include "core/default_modules.hpp"

#include <memory>

#include "services/auth_service.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "services/client_v1_login_gateway_service.hpp"
#include "services/game_gateway_service.hpp"
#include "services/log_service.hpp"
#include "services/login_gateway_service.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"

namespace mir2 {

void register_default_modules(HostRuntime& runtime) {
  runtime.register_module(std::make_unique<LogService>());
  runtime.register_module(std::make_unique<PersistenceService>());
  runtime.register_module(std::make_unique<AuthService>());
  runtime.register_module(std::make_unique<WorldService>());
  if (runtime.context().config.runtime.enable_legacy_gateways) {
    runtime.register_module(std::make_unique<LoginGatewayService>());
    runtime.register_module(std::make_unique<GameGatewayService>());
  }
  if (runtime.context().config.runtime.enable_client_v1_gateways) {
    auto client_v1_admissions = std::make_shared<ClientV1AdmissionRegistry>();
    runtime.register_module(
        std::make_unique<ClientV1LoginGatewayService>(client_v1_admissions));
    runtime.register_module(
        std::make_unique<ClientV1GameGatewayService>(client_v1_admissions));
  }
}

}  // namespace mir2
