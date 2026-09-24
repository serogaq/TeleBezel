#pragma once
#include "telebezel/config.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/runtime/account_store.hpp"
#include "telebezel/runtime/commands.hpp"
#include "telebezel/runtime/cursor_codec.hpp"
#include "telebezel/runtime/request_broker.hpp"
#include "telebezel/runtime/support.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <vector>
namespace telebezel::runtime {
using OperationDeadline = std::chrono::steady_clock::time_point;
class ProxyService final {
public:
  ProxyService(AccountStore &context, const Config &config, TdRequestBroker &broker)
      : context_(context), config_(config), broker_(broker) {}
  void apply_proxy(AccountState &account, OperationDeadline deadline);
  nlohmann::json ping_proxy(const std::string &uuid, const nlohmann::json &proxy);

private:
  AccountStore &context_;
  const Config &config_;
  TdRequestBroker &broker_;
};
class AccountLifecycleService final {
public:
  AccountLifecycleService(AccountStore &context, const Config &config, AccountRegistry &registry,
                          TdTransport &transport, TdRequestBroker &broker, ProxyService &proxy)
      : context_(context), config_(config), registry_(registry), transport_(transport), broker_(broker), proxy_(proxy) {
  }
  AccountState &validated_account(const std::string &uuid, const AccountCommand &command);
  nlohmann::json reconcile(const std::string &uuid, const AccountCommand &command);
  void activate(AccountState &account, OperationDeadline deadline);
  nlohmann::json logout(const std::string &uuid, const AccountCommand &command);
  nlohmann::json update_proxy(const std::string &uuid, const AccountCommand &command);
  nlohmann::json remove(const std::string &uuid, const AccountCommand &command);
  void close_account(AccountState &account, bool destroy, OperationDeadline deadline);

private:
  AccountManifest snapshot_manifest(const AccountState &account) const;
  AccountStore &context_;
  const Config &config_;
  AccountRegistry &registry_;
  TdTransport &transport_;
  TdRequestBroker &broker_;
  ProxyService &proxy_;
};
class AuthorizationService final {
public:
  AuthorizationService(AccountStore &context, TdRequestBroker &broker, AccountLifecycleService &lifecycle)
      : context_(context), broker_(broker), lifecycle_(lifecycle) {}
  nlohmann::json authorization_action(const std::string &uuid, const AuthorizationCommand &command);

private:
  AccountStore &context_;
  TdRequestBroker &broker_;
  AccountLifecycleService &lifecycle_;
};
struct ReadEnvelope {
  std::string updates_cursor;
  std::string connection;
  std::int64_t observed_at{0};
  static ReadEnvelope capture(const CursorCodec &cursors, const AccountState &account, const ReadFence &fence);
  nlohmann::json wrap(nlohmann::json body, const char *source) const;
};
class ReadModelService final {
public:
  ReadModelService(AccountStore &context, const Config &config, TdRequestBroker &broker, CursorCodec &cursors)
      : context_(context), config_(config), broker_(broker), cursors_(cursors) {}
  ~ReadModelService();
  ReadModelService(const ReadModelService &) = delete;
  ReadModelService &operator=(const ReadModelService &) = delete;
  void start();
  void stop();
  bool running() const;
  void cancel_account(const std::string &uuid);
  nlohmann::json chats(const std::string &uuid, const std::string &list, std::size_t limit, const std::string &cursor);
  nlohmann::json chat(const std::string &uuid, std::int64_t chat_id) const;
  nlohmann::json messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit, const std::string &cursor);
  nlohmann::json message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id);
  nlohmann::json preview(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id,
                         const std::string &preview_id);
  nlohmann::json updates(const std::string &uuid, const std::string &cursor, std::size_t limit,
                         std::chrono::seconds wait = std::chrono::seconds(0)) const;
  std::size_t preview_entry_count();

private:
  enum class RefreshKind : std::uint8_t { history, message, preview, evict };
  struct RefreshJob {
    RefreshKind kind{RefreshKind::history};
    std::string uuid;
    std::string epoch;
    std::uint64_t authorization_generation{0};
    std::int64_t chat_id{0};
    std::int64_t message_id{0};
    std::int64_t anchor{0};
    std::size_t limit{0};
    std::string key;
    std::int32_t preview_file_id{0};
    std::size_t preview_size{0};
    std::int32_t client{0};
  };
  struct PreviewEntry {
    enum class State : std::uint8_t { queued, downloading, ready, failed } state{State::queued};
    std::string uuid;
    std::int32_t client{0};
    std::int32_t file_id{0};
    std::size_t reserved{0};
    std::size_t ready_bytes{0};
    unsigned attempts{0};
    std::chrono::steady_clock::time_point retry_at{};
    std::chrono::steady_clock::time_point used_at{};
  };
  struct RefreshOutcome {
    std::string state;
    std::chrono::steady_clock::time_point at{};
  };
  struct HistoryPage {
    std::vector<nlohmann::json> items;
    bool exhausted{false};
    bool failed{false};
    bool deadline{false};
  };
  std::optional<ReadFence> begin_read(const std::string &uuid) const;
  AccountState *fenced_account(const std::string &uuid, const ReadFence &fence) const;
  RefreshJob refresh_job(RefreshKind kind, const std::string &uuid, const ReadFence &fence, std::int64_t chat_id,
                         const std::string &suffix) const;
  HistoryPage fetch_history(std::int32_t client, std::int64_t chat_id, std::int64_t anchor, std::size_t limit,
                            bool only_local, std::chrono::steady_clock::time_point deadline);
  std::string enqueue_refresh(RefreshJob job);
  std::string enqueue_preview(RefreshJob &job, std::chrono::steady_clock::time_point now);
  void remember_outcome(const std::string &key, bool changed);
  void invalidate_preview(const std::string &key);
  bool reserve_preview(const RefreshJob &job);
  void drop_preview(const std::string &key);
  bool trim_preview_entries(std::chrono::steady_clock::time_point now);
  void release_preview(const std::string &key, bool ready, std::size_t bytes);
  void prepare_preview(nlohmann::json &item, const std::string &uuid, const ReadFence &fence);
  void refresh_loop();
  void refresh_once();
  bool run_refresh(const RefreshJob &job);
  bool publish_projections(const RefreshJob &job, const ReadFence &fence, const std::vector<nlohmann::json> &items);
  AccountStore &context_;
  const Config &config_;
  TdRequestBroker &broker_;
  CursorCodec &cursors_;
  mutable std::mutex refresh_mutex_;
  std::condition_variable refresh_condition_;
  std::map<std::string, std::deque<RefreshJob>> refresh_queues_;
  std::deque<std::string> refresh_rotation_;
  std::set<std::string> refresh_keys_;
  std::map<std::string, RefreshOutcome> refresh_results_;
  std::map<std::string, PreviewEntry> preview_entries_;
  std::size_t preview_reserved_bytes_{0};
  std::size_t preview_cache_bytes_{0};
  bool refresh_stopping_{true};
  std::thread refresh_thread_;
};
class InterestLeaseManager final {
public:
  InterestLeaseManager(AccountStore &context, TdRequestBroker &broker) : context_(context), broker_(broker) {}
  ~InterestLeaseManager();
  InterestLeaseManager(const InterestLeaseManager &) = delete;
  InterestLeaseManager &operator=(const InterestLeaseManager &) = delete;
  void start();
  void stop();
  bool running() const;
  nlohmann::json set_interest(const std::string &uuid, std::int64_t chat_id, const std::string &lease_key, bool active,
                              bool await_transition = true);
  nlohmann::json release_interests(const std::string &principal_type, const std::string &principal_id);

private:
  void run();
  void run_once();
  AccountStore &context_;
  TdRequestBroker &broker_;
  std::atomic<bool> stopping_{true};
  std::string rotation_cursor_;
  std::thread worker_;
};
class MessageSendService final {
public:
  MessageSendService(AccountStore &context, TdRequestBroker &broker) : context_(context), broker_(broker) {}
  nlohmann::json send(const std::string &uuid, const SendCommand &command);
  nlohmann::json lookup(const std::string &uuid, const std::vector<std::string> &ids) const;
  static nlohmann::json operation_json(const std::string &id, const SendOperation &operation);
  static void bind_pending(AccountStore &store, AccountState &account, const td_api::message &message);
  static void succeeded(AccountStore &store, AccountState &account, const td_api::updateMessageSendSucceeded &update);
  static void failed(AccountStore &store, AccountState &account, const td_api::updateMessageSendFailed &update);

private:
  static void publish(AccountStore &store, AccountState &account, const std::string &id,
                      const SendOperation &operation);
  static void finish(AccountStore &store, AccountState &account, const std::string &id, const std::string &error,
                     std::int64_t retry_after, bool retryable);
  void dispatch(const std::string &uuid, const SendCommand &command, std::int32_t client, std::int32_t sending_id,
                std::chrono::steady_clock::time_point deadline);
  nlohmann::json await_outcome(const std::string &uuid, const std::string &id,
                               std::chrono::steady_clock::time_point until);
  AccountStore &context_;
  TdRequestBroker &broker_;
};
class RuntimeEngine final {
public:
  RuntimeEngine(AccountStore &context, AccountRegistry &registry, TdTransport &transport, TdRequestBroker &broker)
      : context_(context), registry_(registry), transport_(transport), broker_(broker) {}
  void start();
  void stop();
  StatusSnapshot status() const;
  nlohmann::json snapshots(const std::vector<std::string> &ids) const;
  nlohmann::json snapshot(const std::string &uuid) const;
  void receive_loop();
  void persist_authorizations();
  void handle_update(std::int32_t client_id, td::td_api::Object &object);

private:
  void receive_once(std::chrono::steady_clock::time_point &next_reminder);
  void queue_generation_persist(const AccountState &account);
  void journal_send_capability(AccountState &account, const std::string &kind, std::int64_t peer);
  AccountStore &context_;
  AccountRegistry &registry_;
  TdTransport &transport_;
  std::thread receive_thread_;
  std::thread authorization_thread_;
  std::mutex authorization_mutex_;
  std::condition_variable authorization_condition_;
  std::deque<std::pair<std::string, std::uint64_t>> authorization_queue_;
  bool authorization_stopping_{false};
  std::atomic<bool> receive_done_{false};
  TdRequestBroker &broker_;
};
} // namespace telebezel::runtime
