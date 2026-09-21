#pragma once
#include "telebezel/config.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/runtime/account_store.hpp"
#include "telebezel/runtime/commands.hpp"
#include "telebezel/runtime/cursor_codec.hpp"
#include "telebezel/runtime/request_broker.hpp"
#include <thread>
namespace telebezel::runtime {
class ProxyService final {
public:
  ProxyService(AccountStore &context, const Config &config, TdRequestBroker &broker)
      : context_(context), config_(config), broker_(broker) {}
  void apply_proxy(AccountState &account);
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
  void activate(AccountState &account);
  nlohmann::json logout(const std::string &uuid, const AccountCommand &command);
  nlohmann::json update_proxy(const std::string &uuid, const AccountCommand &command);
  nlohmann::json remove(const std::string &uuid, const AccountCommand &command);
  void close_account(AccountState &account, bool destroy);

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
      : context_(context), config_(config), broker_(broker), cursors_(cursors) {}
  nlohmann::json chats(const std::string &uuid, const std::string &list, std::size_t limit, const std::string &cursor);
  nlohmann::json chat(const std::string &uuid, std::int64_t chat_id) const;
  nlohmann::json messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit, const std::string &cursor);
  nlohmann::json message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id);
  nlohmann::json updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const;

private:
  AccountStore &context_;
  const Config &config_;
  TdRequestBroker &broker_;
  CursorCodec &cursors_;
};
class InterestLeaseManager final {
public:
  InterestLeaseManager(AccountStore &context, TdTransport &transport, TdRequestBroker &broker)
      : context_(context), transport_(transport), broker_(broker) {}
  nlohmann::json set_interest(const std::string &uuid, std::int64_t chat_id, const std::string &lease_key, bool active);
  nlohmann::json release_interests(const std::string &principal_type, const std::string &principal_id);

private:
  AccountStore &context_;
  TdTransport &transport_;
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
  void handle_update(std::int32_t client_id, td::td_api::Object &object);

private:
  AccountStore &context_;
  AccountRegistry &registry_;
  TdTransport &transport_;
  std::thread receive_thread_;
  std::atomic<bool> receive_done_{false};
  TdRequestBroker &broker_;
};
} // namespace telebezel::runtime
