#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <tuple>

namespace telebezel::runtime {
struct InterestChatState {
  enum class Phase : std::uint8_t { Closed, Opening, Open, Closing } phase{Phase::Closed};
  std::uint64_t transition{0};
  std::uint64_t failed_transition{0};
  // A transition TDLib never answered. The chat is then treated as open until a
  // closeChat confirms otherwise, so a late success cannot leak an open chat.
  std::uint64_t unknown_transition{0};
  unsigned close_failures{0};
  std::chrono::steady_clock::time_point retry_at{};
};
// Records where each ordering change happened so a cursor is only rejected when
// something at or above its boundary moved; appending at the tail is harmless.
struct ChatOrderLog {
  static constexpr std::size_t retained = 512;
  std::uint64_t version{0};
  // Cursors issued at or below `base` can no longer be judged precisely.
  std::uint64_t base{0};
  std::deque<std::pair<std::uint64_t, std::int64_t>> entries;

  void record(std::int64_t order) {
    ++version;
    entries.emplace_back(version, order);
    if (entries.size() > retained) {
      base = entries.front().first;
      entries.pop_front();
    }
  }
  void reset() {
    ++version;
    base = version;
    entries.clear();
  }
  bool disturbed(std::uint64_t since, std::int64_t boundary_order) const {
    if (since < base)
      return true;
    for (const auto &[recorded, order] : entries)
      if (recorded > since && order >= boundary_order)
        return true;
    return false;
  }
};
struct CachedMessageMeta {
  std::size_t bytes{0};
  std::int64_t date{0};
};
struct AccountState {
  using MessageKey = std::pair<std::int64_t, std::int64_t>;
  std::uint64_t revision{0};
  std::uint64_t applied_revision{0};
  std::uint64_t authorization_generation{1};
  std::uint64_t ready_generation{0};
  std::chrono::steady_clock::time_point resend_available_at{};
  std::chrono::steady_clock::time_point discovered_at{std::chrono::steady_clock::now()};
  std::size_t message_bytes{0};
  std::size_t sender_bytes{0};
  std::uint64_t event_sequence{0};
  nlohmann::json proxy = {{"mode", "inherit"}, {"http_only", false}};
  // What TDLib is actually running with, kept apart from the desired
  // configuration above so a failed step is resumed on the next reconciliation.
  nlohmann::json applied_proxy;
  nlohmann::json telegram_identity;
  std::string uuid;
  std::string generation;
  std::string lifecycle{"provisioning"};
  std::string authorization_state{"awaiting_reconciliation"};
  std::string connection_state{"unknown"};
  std::string authorization_version{"0"};
  std::string authorization_fingerprint;
  std::string qr_link;
  std::string last_error;
  std::string operation_id;
  std::string operation_phase;
  std::string revision_fingerprint;
  std::string effective_config_id;
  std::string telegram_api_hash;
  std::string applied_telegram_api_hash;
  std::string delivery_method;
  std::string runtime_epoch;
  std::string main_load_error;
  std::string archive_load_error;
  std::map<std::int64_t, nlohmann::json> chats;
  std::map<MessageKey, nlohmann::json> messages;
  std::map<MessageKey, CachedMessageMeta> message_meta;
  std::map<std::int64_t, std::size_t> chat_message_counts;
  std::set<std::tuple<std::int64_t, std::int64_t, std::int64_t>> message_age;
  std::map<std::string, std::string> sender_names;
  std::map<std::string, std::pair<std::int64_t, std::chrono::steady_clock::time_point>> interests;
  std::map<std::int64_t, std::size_t> interest_counts;
  std::map<std::int64_t, InterestChatState> interest_states;
  std::deque<std::string> sender_order;
  std::deque<nlohmann::json> events;
  ChatOrderLog main_orders;
  ChatOrderLog archive_orders;
  std::int32_t client_id{0};
  std::int32_t telegram_api_id{0};
  std::int32_t applied_telegram_api_id{0};
  bool generation_unpersisted{false};
  bool use_test_dc{false};
  bool proxy_applied{false};
  bool delivery_supported{true};
  bool reconciled{false};
  bool closed{false};
  bool closing{false};
  bool tombstone{false};
  bool busy{false};
  bool main_exhausted{false};
  bool archive_exhausted{false};
};
} // namespace telebezel::runtime
