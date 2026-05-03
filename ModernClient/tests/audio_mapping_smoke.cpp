#include <cassert>
#include <filesystem>
#include <iostream>

#include "audio/audio_id_mapping.hpp"
#include "audio/sound_constants.hpp"

namespace {

std::filesystem::path asset_root() {
  const std::filesystem::path root = LR"(F:\mir2\Legend of Mir)";
  assert(std::filesystem::exists(root / L"Wav" / L"sound.lst"));
  return root;
}

}  // namespace

int main() {
  using namespace mir2::client;

  AudioIdMapping mapping;
  const auto root = asset_root();
  assert(mapping.load_from_file(root / L"Wav" / L"sound.lst"));

  const auto* one = mapping.path_for(1);
  assert(one != nullptr);
  assert(*one == L"wav\\1.wav");

  const auto* intro = mapping.path_for(s_intro_theme);
  assert(intro != nullptr);
  assert(*intro == L"wav\\102.wav");

  const auto* magic = mapping.path_for(sound_id_magic_base(1));
  assert(magic != nullptr);
  assert(*magic == L"wav\\M1-1.wav");

  const auto* hole = mapping.path_for(140);
  assert(hole != nullptr);
  assert(hole->empty());

  const int base = mapping.dynamic_base();
  assert(base > 0);
  assert(s_FireFlower_1 == base);
  assert(s_FireFlower_2 == base + 1);
  assert(s_FireFlower_3 == base + 2);
  assert(s_HeroLogIn == base + 3);
  assert(s_HeroLogOut == base + 4);
  assert(s_hero_shield == base + 11);
  assert(s_powerup == base + 12);
  assert(s_hit_ZRJF_M == base + 13);
  assert(s_hit_ZRJF_w == base + 14);
  assert(s_cboZs1_start_m == base + 15);
  assert(s_cboFs1_start == base + 21);
  assert(s_cboDs4_target == base + 36);

  const auto* fire_flower = mapping.path_for(s_FireFlower_1);
  assert(fire_flower != nullptr);
  assert(*fire_flower == L"wav\\newysound1.wav");

  const auto* hero_login = mapping.path_for(s_HeroLogIn);
  assert(hero_login != nullptr);
  assert(*hero_login == L"wav\\HeroLogin.wav");

  const auto* powerup = mapping.path_for(s_powerup);
  assert(powerup != nullptr);
  assert(*powerup == L"wav\\powerup.wav");

  const auto* zrjf = mapping.path_for(s_hit_ZRJF_M);
  assert(zrjf != nullptr);
  assert(*zrjf == L"wav\\M56-0.wav");

  const auto* combo_start = mapping.path_for(s_cboZs1_start_m);
  assert(combo_start != nullptr);
  assert(*combo_start == L"wav\\cboZs1_start_m.wav");

  const auto* combo_target = mapping.path_for(s_cboDs4_target);
  assert(combo_target != nullptr);
  assert(*combo_target == L"wav\\cboDs4_target.wav");

  const auto* missing = mapping.path_for(10110);
  assert(missing != nullptr);
  assert(*missing == L"wav\\M11-1.wav");
  assert(!std::filesystem::exists(mapping.resolve_path(root, *missing)));

  std::cout << "audio_mapping_smoke ok, dynamic_base=" << base
            << ", size=" << mapping.size() << "\n";
  return 0;
}
