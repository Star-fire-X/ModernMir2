#include <cassert>
#include <string>

#include "text/encoding.hpp"

int main() {
  using namespace mir2::client::text;

  const std::string utf8 = "\xE4\xB8\xAD\xE6\x96\x87\xE8\x81\x8A\xE5\xA4\xA9/NPC";
  const std::wstring wide = L"\x4E2D\x6587\x804A\x5929/NPC";

  assert(utf8_to_wide(utf8) == wide);
  assert(wide_to_utf8(wide) == utf8);

  const std::string invalid_utf8 = "\xD6\xD0\xCE\xC4";
  assert(!utf8_to_wide(invalid_utf8).empty());

  return 0;
}
