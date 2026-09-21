#pragma once
#include "telebezel/config.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/runtime/account_store.hpp"
#include "telebezel/runtime/commands.hpp"
#include "telebezel/runtime/cursor_codec.hpp"
#include "telebezel/runtime/request_broker.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <set>
#include <thread>
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
class ReadModelService final {
public:
  ReadModelService(AccountStore &context, const Config &config, TdRequestBroker &broker, CursorCodec &cursors)
      : context_(context), config_(config), broker_(broker), cursors_(cursors),
        refresh_thread_(&ReadModelService::refresh_loop, this) {}
  ~ReadModelService();
  void cancel_account(const std::string &uuid);
  nlohmann::json chats(const std::string &uuid, const std::string &list, std::size_t limit, const std::string &cursor);
  nlohmann::json chat(const std::string &uuid, std::int64_t chat_id) const;
  nlohmann::json messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit, const std::string &cursor);
  nlohmann::json message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id);
  nlohmann::json preview(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id,
                         const std::string &preview_id);
  nlohmann::json updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const;

private:
  struct RefreshJob {
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
  };
  std::string enqueue_refresh(RefreshJob job);
  void prepare_preview(nlohmann::json &item, const std::string &uuid, const std::string &generation,
                       const std::string &epoch, std::uint64_t authorization_generation);
  void refresh_loop();
  AccountStore &context_;
  const Config &config_;
  TdRequestBroker &broker_;
  CursorCodec &cursors_;
  std::mutex refresh_mutex_;
  std::condition_variable refresh_condition_;
  std::deque<RefreshJob> refresh_queue_;
  std::set<std::string> refresh_keys_;
  std::map<std::string, std::size_t> preview_reservations_;
  std::size_t preview_reserved_bytes_{0};
  bool refresh_stopping_{false};
  std::thread refresh_thread_;
};
class InterestLeaseManager final {
public:
  InterestLeaseManager(AccountStore &context, TdRequestBroker &broker)
      : context_(context), broker_(broker), worker_(&InterestLeaseManager::run, this) {}
  ~InterestLeaseManager();
  nlohmann::json set_interest(const std::string &uuid, std::int64_t chat_id, const std::string &lease_key, bool active,
                              bool await_transition = true);
  nlohmann::json release_interests(const std::string &principal_type, const std::string &principal_id);

private:
  void run();
  AccountStore &context_;
  TdRequestBroker &broker_;
  std::atomic<bool> stopping_{false};
  std::thread worker_;
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
