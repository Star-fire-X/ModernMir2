#pragma once

namespace mir2::client {

struct AudioSettings {
  bool sound_enabled = true;
  bool bgm_enabled = true;
  int sound_volume = 100;
  int bgm_volume = 100;
};

}  // namespace mir2::client
