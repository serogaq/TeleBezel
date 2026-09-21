#pragma once
#include "telebezel/config.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/status.hpp"
#include "telebezel/transport.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace telebezel {
class TdRuntime final {
public:
  TdRuntime(const Config &config, Registry &registry, std::unique_ptr<TdTransport> transport = {});
  ~TdRuntime();
  TdRuntime(const TdRuntime &) = delete;
  TdRuntime &operator=(const TdRuntime &) = delete;
  void start();
  void stop();
  StatusSnapshot status() const;
  nlohmann::json snapshots(const std::vector<std::string> &ids) const;
  nlohmann::json snapshot(const std::string &uuid) const;
  nlohmann::json reconcile(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json authorization_action(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json logout(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json update_proxy(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json ping_proxy(const std::string &uuid, const nlohmann::json &proxy);
  nlohmann::json remove(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json chats(const std::string &uuid, const std::string &list, std::size_t limit, const std::string &cursor);
  nlohmann::json chat(const std::string &uuid, std::int64_t chat_id) const;
  nlohmann::json messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit, const std::string &cursor);
  nlohmann::json message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id);
  nlohmann::json updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const;
  nlohmann::json set_interest(const std::string &uuid, std::int64_t chat_id, const std::string &lease_key, bool active);
  nlohmann::json release_interests(const std::string &principal_type, const std::string &principal_id);

private:
  struct Account {
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
  struct Pending {
    std::int32_t client_id;
    std::promise<td::td_api::object_ptr<td::td_api::Object>> promise;
  };
  struct Unresolved {
    std::int32_t client_id;
    std::chrono::steady_clock::time_point recovery_at;
    bool recover_client;
  };
  struct RequestHandle {
    std::uint64_t request_id;
    std::int32_t client_id;
    std::future<td::td_api::object_ptr<td::td_api::Object>> future;
    bool pending;
  };
  enum class AsyncRequestKind { get_me };
  struct AsyncRequest {
    std::int32_t client_id;
    AsyncRequestKind kind;
    std::chrono::steady_clock::time_point deadline;
  };
  void receive_loop();
  void handle_update(std::int32_t client_id, td::td_api::Object &object);
  td::td_api::object_ptr<td::td_api::Object> request(std::int32_t client_id,
                                                     td::td_api::object_ptr<td::td_api::Function> function,
                                                     std::chrono::seconds timeout = std::chrono::seconds(8),
                                                     bool recover_stalled_client = false);
  RequestHandle begin_request(std::int32_t client_id, td::td_api::object_ptr<td::td_api::Function> function);
  td::td_api::object_ptr<td::td_api::Object> await_request(RequestHandle handle, std::chrono::seconds timeout,
                                                           bool recover_stalled_client = false);
  void request_identity(Account &account);
  void append_event(Account &account, const std::string &type, std::int64_t chat_id, std::int64_t message_id = 0);
  std::string cursor_for(const Account &account, std::uint64_t sequence) const;
  std::optional<std::uint64_t> parse_cursor(const Account &account, const std::string &cursor) const;
  std::uint64_t next_id();
  void activate(Account &account);
  void close_account(Account &account, bool destroy);
  void apply_proxy(Account &account);
  nlohmann::json account_json(const Account &account) const;
  Account &validated_account(const std::string &uuid, const nlohmann::json &command);
  static std::string authorization_name(std::int32_t id);
  static std::string connection_name(std::int32_t id);
  static std::vector<std::string> allowed_actions(const Account &account);
  static nlohmann::json safe_error(const std::string &code, int status);
  const Config &config_;
  Registry &registry_;
  std::unique_ptr<TdTransport> transport_;
  std::thread receive_thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> receive_done_{false};
  std::atomic<std::uint64_t> next_request_id_{1};
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  StatusSnapshot status_;
  std::map<std::string, Account> accounts_;
  std::unordered_map<std::int32_t, std::string> client_accounts_;
  std::unordered_map<std::uint64_t, Pending> pending_;
  std::unordered_map<std::uint64_t, Unresolved> unresolved_;
  std::unordered_map<std::uint64_t, AsyncRequest> async_requests_;
  std::set<std::string> orphan_accounts_;
  std::map<std::string, std::string> manifest_errors_;
};
} // namespace telebezel
