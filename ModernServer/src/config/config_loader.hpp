#pragma once

#include <filesystem>

#include "config/models.hpp"

namespace mir2 {

class ConfigLoader {
 public:
  HostConfig load(const std::filesystem::path& root) const;
};

}  // namespace mir2
