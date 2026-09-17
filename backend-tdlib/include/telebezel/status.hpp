#pragma once
#include <cstddef>
#include <string>

namespace telebezel {
struct StatusSnapshot {
  bool ready{false};
  std::string tdlib_version;
  std::size_t account_count{0};
};
std::string make_request_id();
std::string health_json();
std::string status_json(const StatusSnapshot &status, const std::string &request_id);
bool token_matches(const std::string &expected, const std::string &provided);
} // namespace telebezel
