#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace telebezel::runtime {
struct AccountState {
  std::string uuid;
  std::string generation;
  std::int32_t client_id{0};
  std::uint64_t revision{0};
  std::uint64_t authorization_generation{1};
  bool use_test_dc{false};
  std::string lifecycle{"provisioning"};
  std::string authorization_state{"awaiting_reconciliation"};
  std::string connection_state{"unknown"};
  std::string authorization_version{"0"};
  std::string qr_link;
  std::string last_error;
  std::string operation_id;
  std::string operation_phase;
  std::string revision_fingerprint;
  std::string effective_config_id;
  std::int32_t telegram_api_id{0};
  std::string telegram_api_hash;
  nlohmann::json proxy = {{"mode", "inherit"}, {"http_only", false}};
  nlohmann::json telegram_identity;
  std::string delivery_method;
  bool delivery_supported{true};
  std::chrono::steady_clock::time_point resend_available_at{};
  bool reconciled{false};
  bool closed{false};
  bool closing{false};
  bool tombstone{false};
  bool busy{false};
  std::chrono::steady_clock::time_point discovered_at{std::chrono::steady_clock::now()};
  std::map<std::int64_t, nlohmann::json> chats;
  std::map<std::pair<std::int64_t, std::int64_t>, nlohmann::json> messages;
  std::deque<nlohmann::json> events;
  std::uint64_t event_sequence{0};
  std::string runtime_epoch;
  std::map<std::string, std::pair<std::int64_t, std::chrono::steady_clock::time_point>> interests;
  std::map<std::int64_t, std::size_t> interest_counts;
};
} // namespace telebezel::runtime
