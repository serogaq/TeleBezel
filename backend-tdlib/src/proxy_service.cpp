#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace {
struct ResolvedProxy {
  bool direct{false};
  ProxyConfig config;
};

// Runs to completion before the first TDLib call, so an unusable configuration
// cannot strand the client without a proxy or silently make it go direct.
ResolvedProxy resolve_proxy(const Config &config, const nlohmann::json &desired) {
  const std::string mode = desired.value("mode", "inherit");
  if (mode != "inherit" && mode != "direct" && !valid_uuid(desired.value("id", "")))
    throw std::runtime_error("configuration.invalid");
  ResolvedProxy resolved;
  if (mode == "inherit") {
    resolved.config = config.proxy;
  } else if (mode == "direct") {
    resolved.direct = true;
    return resolved;
  } else {
    resolved.config.mode = parse_proxy_mode(mode);
    resolved.config.host = desired.value("host", "");
    resolved.config.port = desired.value("port", 0);
    resolved.config.http_only = desired.value("http_only", false);
    resolved.config.username = desired.value("username", "");
    resolved.config.password = desired.value("password", "");
    resolved.config.secret = desired.value("secret", "");
  }
  if (resolved.config.mode == ProxyMode::direct || resolved.config.mode == ProxyMode::inherit) {
    resolved.direct = true;
    return resolved;
  }
  const auto &selected = resolved.config;
  if (selected.host.empty() || selected.port == 0 ||
      (selected.mode == ProxyMode::mtproto &&
       (selected.secret.empty() || !selected.username.empty() || !selected.password.empty())) ||
      ((selected.mode == ProxyMode::socks5 || selected.mode == ProxyMode::http) && !selected.secret.empty()) ||
      (!selected.password.empty() && selected.username.empty()) ||
      (selected.mode != ProxyMode::http && selected.http_only)) {
    throw std::runtime_error("configuration.invalid");
  }
  return resolved;
}

td_api::object_ptr<td_api::ProxyType> proxy_type(const ProxyConfig &selected) {
  if (selected.mode == ProxyMode::socks5)
    return td_api::make_object<td_api::proxyTypeSocks5>(selected.username, selected.password);
  if (selected.mode == ProxyMode::http)
    return td_api::make_object<td_api::proxyTypeHttp>(selected.username, selected.password, selected.http_only);
  return td_api::make_object<td_api::proxyTypeMtproto>(selected.secret);
}
} // namespace

void ProxyService::apply_proxy(Account &account, OperationDeadline deadline) {
  std::int32_t client = 0;
  nlohmann::json desired;
  {
    std::lock_guard lock(context_.mutex_);
    client = account.client_id;
    desired = account.proxy;
  }
  const auto resolved = resolve_proxy(config_, desired);
  std::int32_t installed = 0;
  if (resolved.direct) {
    const auto disabled =
        broker_.request(client, td_api::make_object<td_api::disableProxy>(), operation_budget(deadline));
    throw_runtime_control_error(disabled);
    if (!disabled || disabled->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
  } else {
    // enable=true switches TDLib over in one step; the working profile stays in
    // place until the new one is accepted.
    auto proxy =
        td_api::make_object<td_api::proxy>(resolved.config.host, resolved.config.port, proxy_type(resolved.config));
    const auto response =
        broker_.request(client, td_api::make_object<td_api::addProxy>(std::move(proxy), true, "telebezel-managed"),
                        operation_budget(deadline));
    throw_runtime_control_error(response);
    if (!response || response->get_id() != td_api::addedProxy::ID)
      throw std::runtime_error("configuration.invalid");
    installed = static_cast<const td_api::addedProxy &>(*response).id_;
  }
  // The profiles installed earlier are already inactive, so failing to retire
  // one is reported but must not undo the switch.
  auto existing = broker_.request(client, td_api::make_object<td_api::getProxies>(), operation_budget(deadline));
  if (existing && existing->get_id() == td_api::addedProxies::ID) {
    auto proxies = td::move_tl_object_as<td_api::addedProxies>(existing);
    for (const auto &proxy : proxies->proxies_) {
      if (!proxy || proxy->comment_ != "telebezel-managed" || proxy->id_ == installed)
        continue;
      const auto removed =
          broker_.request(client, td_api::make_object<td_api::removeProxy>(proxy->id_), operation_budget(deadline));
      if (!removed || removed->get_id() == td_api::error::ID)
        std::cerr << nlohmann::json{{"event", "proxy_cleanup_failed"},
                                    {"account_uuid", account.uuid},
                                    {"proxy_id", proxy->id_}}
                         .dump()
                  << '\n';
    }
  }
  std::lock_guard lock(context_.mutex_);
  account.applied_proxy = desired;
  account.proxy_applied = true;
}

nlohmann::json ProxyService::ping_proxy(const std::string &uuid, const nlohmann::json &proxy_config) {
  std::int32_t client = 0;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || found->second.tombstone)
      return safe_error("account.not_found", 404);
    if (found->second.client_id == 0 || found->second.closed)
      return safe_error("service.busy", 503);
    client = found->second.client_id;
  }
  const auto mode = proxy_config.value("mode", "");
  const auto host = proxy_config.value("host", "");
  const auto port = proxy_config.value("port", 0);
  const auto username = proxy_config.value("username", "");
  const auto password = proxy_config.value("password", "");
  const auto secret = proxy_config.value("secret", "");
  const auto http_only = proxy_config.value("http_only", false);
  if (host.empty() || port < 1 || port > 65535 ||
      (mode == "mtproto" && (secret.empty() || !username.empty() || !password.empty())) ||
      ((mode == "socks5" || mode == "http") && !secret.empty()) ||
      (mode != "socks5" && mode != "http" && mode != "mtproto") || (!password.empty() && username.empty()) ||
      (mode != "http" && http_only))
    return safe_error("configuration.invalid", 422);
  td_api::object_ptr<td_api::ProxyType> type;
  if (mode == "socks5")
    type = td_api::make_object<td_api::proxyTypeSocks5>(username, password);
  else if (mode == "http")
    type = td_api::make_object<td_api::proxyTypeHttp>(username, password, http_only);
  else
    type = td_api::make_object<td_api::proxyTypeMtproto>(secret);
  auto proxy = td_api::make_object<td_api::proxy>(host, port, std::move(type));
  auto response = broker_.request(client, td_api::make_object<td_api::pingProxy>(std::move(proxy)));
  if (!response || response->get_id() != td_api::seconds::ID)
    return safe_error("proxy.unreachable", 502);
  const auto seconds = td::move_tl_object_as<td_api::seconds>(response);
  return {{"latency_ms", static_cast<std::int64_t>(std::llround(seconds->seconds_ * 1000.0))}};
}

} // namespace telebezel::runtime
