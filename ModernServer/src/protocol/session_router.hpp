#pragma once

#include <string>

namespace mir2 {

class SessionRouter {
 public:
  [[nodiscard]] std::string route_ingress(const std::string& gateway_name) const;
};

}  // namespace mir2
