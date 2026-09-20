#pragma once
#include "telebezel/config.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/status.hpp"
#include "telebezel/transport.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
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
  nlohmann::json remove(const std::string &uuid, const nlohmann::json &command);

private:
  struct Account {
    std::string uuid;
    std::string generation;
    std::int32_t client_id{0};
    std::uint64_t revision{0};
    bool use_test_dc{false};
    std::string lifecycle{"provisioning"};
    std::string authorization_state{"awaiting_reconciliation"};
    std::string connection_state{"unknown"};
    std::string authorization_version{"0"};
    std::string qr_link;
    std::string last_error;
    std::string operation_id;
    std::string revision_fingerprint;
    nlohmann::json proxy = {{"mode", "inherit"}, {"http_only", false}};
    nlohmann::json telegram_identity;
    std::string delivery_method;
    bool delivery_supported{true};
    std::chrono::steady_clock::time_point resend_available_at{};
    bool reconciled{false};
    bool closed{false};
    bool tombstone{false};
    bool busy{false};
    std::chrono::steady_clock::time_point discovered_at{std::chrono::steady_clock::now()};
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
