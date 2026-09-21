#include "telebezel/td_runtime.hpp"
#include "telebezel/runtime/components.hpp"
namespace telebezel {
struct TdRuntime::Impl {
  std::unique_ptr<TdTransport> transport;
  runtime::AccountStore context;
  runtime::TdRequestBroker broker;
  runtime::CursorCodec cursors;
  runtime::ProxyService proxy;
  runtime::AccountLifecycleService lifecycle;
  runtime::AuthorizationService authorization;
  runtime::ReadModelService reads;
  runtime::InterestLeaseManager interests;
  runtime::RuntimeEngine engine;
  Impl(const Config &config, AccountRegistry &registry, std::unique_ptr<TdTransport> transport)
      : transport(transport ? std::move(transport) : std::make_unique<NativeTdTransport>()), context(config),
        broker(*this->transport), cursors(config.internal_token), proxy(context, config, broker),
        lifecycle(context, config, registry, *this->transport, broker, proxy),
        authorization(context, broker, lifecycle), reads(context, config, broker, cursors), interests(context, broker),
        engine(context, registry, *this->transport, broker) {}
};
TdRuntime::TdRuntime(const Config &config, AccountRegistry &registry, std::unique_ptr<TdTransport> transport)
    : impl_(std::make_unique<Impl>(config, registry, std::move(transport))) {}
TdRuntime::~TdRuntime() { stop(); }
void TdRuntime::start() { impl_->engine.start(); }
void TdRuntime::stop() { impl_->engine.stop(); }
StatusSnapshot TdRuntime::status() const { return impl_->engine.status(); }
nlohmann::json TdRuntime::snapshots(const std::vector<std::string> &ids) const { return impl_->engine.snapshots(ids); }
nlohmann::json TdRuntime::snapshot(const std::string &uuid) const { return impl_->engine.snapshot(uuid); }
nlohmann::json TdRuntime::reconcile(const std::string &uuid, const nlohmann::json &command) {
  return impl_->lifecycle.reconcile(uuid, runtime::AccountCommand::parse(uuid, command));
}
nlohmann::json TdRuntime::authorization_action(const std::string &uuid, const nlohmann::json &command) {
  return impl_->authorization.authorization_action(uuid, runtime::AuthorizationCommand::parse(uuid, command));
}
nlohmann::json TdRuntime::logout(const std::string &uuid, const nlohmann::json &command) {
  auto parsed = runtime::AccountCommand::parse(uuid, command);
  impl_->reads.cancel_account(uuid);
  return impl_->lifecycle.logout(uuid, parsed);
}
nlohmann::json TdRuntime::update_proxy(const std::string &uuid, const nlohmann::json &command) {
  return impl_->lifecycle.update_proxy(uuid, runtime::AccountCommand::parse(uuid, command));
}
nlohmann::json TdRuntime::ping_proxy(const std::string &uuid, const nlohmann::json &proxy) {
  return impl_->proxy.ping_proxy(uuid, proxy);
}
nlohmann::json TdRuntime::remove(const std::string &uuid, const nlohmann::json &command) {
  auto parsed = runtime::AccountCommand::parse(uuid, command);
  impl_->reads.cancel_account(uuid);
  return impl_->lifecycle.remove(uuid, parsed);
}
nlohmann::json TdRuntime::chats(const std::string &uuid, const std::string &list, std::size_t limit,
                                const std::string &cursor) {
  return impl_->reads.chats(uuid, list, limit, cursor);
}
nlohmann::json TdRuntime::chat(const std::string &uuid, std::int64_t chat_id) const {
  return impl_->reads.chat(uuid, chat_id);
}
nlohmann::json TdRuntime::messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit,
                                   const std::string &cursor) {
  return impl_->reads.messages(uuid, chat_id, limit, cursor);
}
nlohmann::json TdRuntime::message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id) {
  return impl_->reads.message(uuid, chat_id, message_id);
}
nlohmann::json TdRuntime::preview(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id,
                                  const std::string &preview_id) {
  return impl_->reads.preview(uuid, chat_id, message_id, preview_id);
}
nlohmann::json TdRuntime::updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const {
  return impl_->reads.updates(uuid, cursor, limit);
}
nlohmann::json TdRuntime::set_interest(const std::string &uuid, std::int64_t chat_id, const std::string &lease_key,
                                       bool active, bool await_transition) {
  return impl_->interests.set_interest(uuid, chat_id, lease_key, active, await_transition);
}
nlohmann::json TdRuntime::release_interests(const std::string &principal_type, const std::string &principal_id) {
  return impl_->interests.release_interests(principal_type, principal_id);
}
} // namespace telebezel
