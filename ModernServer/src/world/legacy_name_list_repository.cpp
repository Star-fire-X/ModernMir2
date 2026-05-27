#include "world/legacy_name_list_repository.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

#include "util/string_utils.hpp"

namespace mir2 {

namespace {

std::string escape_file_component(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string escaped;
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) != 0 || ch == '-' || ch == '_' || ch == '.') {
      escaped.push_back(static_cast<char>(ch));
      continue;
    }
    escaped.push_back('%');
    escaped.push_back(kHex[(byte >> 4) & 0x0f]);
    escaped.push_back(kHex[byte & 0x0f]);
  }
  return escaped.empty() ? std::string{"default"} : escaped;
}

}  // namespace

LegacyNameListRepository::LegacyNameListRepository(std::filesystem::path root)
    : root_(std::move(root)) {}

bool LegacyNameListRepository::contains(std::string_view list_name, std::string_view subject) {
  const auto key = normalize_key(list_name);
  const auto wanted = normalize_subject(subject);
  const auto& list = mutable_list(key);
  return list.find(wanted) != list.end();
}

std::size_t LegacyNameListRepository::add(std::string_view list_name, std::string_view subject) {
  const auto key = normalize_key(list_name);
  auto& list = mutable_list(key);
  list.insert(normalize_subject(subject));
  save(key);
  return list.size();
}

std::size_t LegacyNameListRepository::remove(std::string_view list_name, std::string_view subject) {
  const auto key = normalize_key(list_name);
  auto& list = mutable_list(key);
  list.erase(normalize_subject(subject));
  save(key);
  return list.size();
}

std::size_t LegacyNameListRepository::size(std::string_view list_name) {
  const auto key = normalize_key(list_name);
  return mutable_list(key).size();
}

std::string LegacyNameListRepository::normalize_key(std::string_view value) {
  return util::lower_copy(util::trim(std::string(value)));
}

std::string LegacyNameListRepository::normalize_subject(std::string_view value) {
  return util::lower_copy(util::trim(std::string(value)));
}

std::filesystem::path LegacyNameListRepository::list_path(std::string_view key) const {
  return root_ / (escape_file_component(key) + ".txt");
}

std::unordered_set<std::string>& LegacyNameListRepository::mutable_list(std::string_view key) {
  const auto normalized = normalize_key(key);
  if (loaded_.find(normalized) == loaded_.end()) {
    load(normalized);
  }
  return lists_[normalized];
}

void LegacyNameListRepository::load(std::string_view key) {
  const auto normalized = normalize_key(key);
  loaded_.insert(normalized);
  if (root_.empty()) {
    return;
  }
  std::ifstream file(list_path(normalized), std::ios::binary);
  if (!file) {
    return;
  }
  auto& list = lists_[normalized];
  std::string line;
  while (std::getline(file, line)) {
    auto subject = normalize_subject(line);
    if (!subject.empty()) {
      list.insert(std::move(subject));
    }
  }
}

void LegacyNameListRepository::save(std::string_view key) const {
  const auto normalized = normalize_key(key);
  if (root_.empty()) {
    return;
  }
  std::error_code ignored;
  std::filesystem::create_directories(root_, ignored);
  std::ofstream file(list_path(normalized), std::ios::binary | std::ios::trunc);
  if (!file) {
    return;
  }
  const auto list_it = lists_.find(normalized);
  if (list_it == lists_.end()) {
    return;
  }
  std::vector<std::string> subjects(list_it->second.begin(), list_it->second.end());
  std::sort(subjects.begin(), subjects.end());
  for (const auto& subject : subjects) {
    file << subject << '\n';
  }
}

}  // namespace mir2
