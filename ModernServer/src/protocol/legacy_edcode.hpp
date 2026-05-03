#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_types.hpp"

namespace mir2 {

std::string legacy_encode_string(std::string_view text);
std::string legacy_decode_string(std::string_view encoded);
std::string legacy_encode_buffer(const void* data, std::size_t size);
bool legacy_decode_buffer(std::string_view encoded, void* data, std::size_t size);
std::string legacy_encode_message(const LegacyDefaultMessage& message);
std::optional<LegacyDefaultMessage> legacy_decode_message(std::string_view encoded);
std::size_t legacy_message_encoded_size();

}  // namespace mir2
