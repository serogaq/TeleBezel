#include "telebezel/td_runtime.hpp"
#include "telebezel/crypto.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <td/telegram/td_api.h>

namespace telebezel {
namespace td_api = td::td_api;
namespace {
bool td_error_code(const td_api::object_ptr<td_api::Object> &object, int code) {
  return object && object->get_id() == td_api::error::ID && static_cast<const td_api::error &>(*object).code_ == code;
}
bool td_error_message(const td_api::object_ptr<td_api::Object> &object, const std::string &message) {
  return object && object->get_id() == td_api::error::ID &&
         static_cast<const td_api::error &>(*object).message_ == message;
}
void throw_runtime_control_error(const td_api::object_ptr<td_api::Object> &object) {
  if (td_error_code(object, 504))
    throw std::runtime_error("operation.outcome_unknown");
  if (td_error_message(object, "service.busy"))
    throw std::runtime_error("service.busy");
  if (td_error_message(object, "service.stopping"))
    throw std::runtime_error("service.stopping");
}
std::string nullable_string(const nlohmann::json &value, const char *key) {
  const auto iterator = value.find(key);
  return iterator != value.end() && iterator->is_string() ? iterator->get<std::string>() : std::string{};
}
std::string command_fingerprint(nlohmann::json command) {
  command.erase("mode");
  command.erase("lifecycle");
  command.erase("operation_id");
  if (command.contains("proxy") && command["proxy"].is_object()) {
    const auto &proxy = command["proxy"];
    command["proxy"] = proxy.contains("id") ? nlohmann::json{{"id", proxy["id"]}}
                                            : nlohmann::json{{"mode", proxy.value("mode", "inherit")}};
  }
  return sha256_hex(command.dump());
}
std::pair<std::string, bool> delivery_method(const td_api::AuthenticationCodeType *type) {
  if (type == nullptr)
    return {"unknown", false};
  if (type->get_id() == td_api::authenticationCodeTypeTelegramMessage::ID)
    return {"telegram_message", true};
  if (type->get_id() == td_api::authenticationCodeTypeSms::ID)
    return {"sms", true};
  if (type->get_id() == td_api::authenticationCodeTypeSmsWord::ID)
    return {"sms_word", true};
  if (type->get_id() == td_api::authenticationCodeTypeSmsPhrase::ID)
    return {"sms_phrase", true};
  if (type->get_id() == td_api::authenticationCodeTypeCall::ID)
    return {"call", true};
  if (type->get_id() == td_api::authenticationCodeTypeFlashCall::ID)
    return {"flash_call", true};
  if (type->get_id() == td_api::authenticationCodeTypeMissedCall::ID)
    return {"missed_call", true};
  if (type->get_id() == td_api::authenticationCodeTypeFragment::ID)
    return {"fragment", true};
  if (type->get_id() == td_api::authenticationCodeTypeFirebaseAndroid::ID ||
      type->get_id() == td_api::authenticationCodeTypeFirebaseIos::ID)
    return {"firebase", false};
  return {"unknown", false};
}
} // namespace

TdRuntime::TdRuntime(const Config &config, Registry &registry, std::unique_ptr<TdTransport> transport)
    : config_(config), registry_(registry),
      transport_(transport ? std::move(transport) : std::make_unique<NativeTdTransport>()) {}
TdRuntime::~TdRuntime() { stop(); }

void TdRuntime::start() {
  if (receive_thread_.joinable())
    return;
  td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(0));
  const auto version_object = td::ClientManager::execute(td_api::make_object<td_api::getOption>("version"));
  if (!version_object || version_object->get_id() != td_api::optionValueString::ID) {
    throw std::runtime_error("unable to read TDLib version");
  }
  const auto &version = static_cast<const td_api::optionValueString &>(*version_object);
  {
    std::lock_guard lock(mutex_);
    status_.ready = true;
    status_.tdlib_version = version.value_;
    for (const auto &manifest : registry_.discover()) {
      Account account;
      account.uuid = manifest.uuid;
      account.generation = manifest.generation;
      account.revision = manifest.revision;
      account.use_test_dc = manifest.use_test_dc;
      account.lifecycle = manifest.lifecycle;
      account.operation_id = manifest.operation_id;
      account.revision_fingerprint = manifest.content_hash;
      account.proxy = manifest.proxy;
      account.tombstone = manifest.tombstone;
      account.closed = manifest.tombstone || manifest.lifecycle == "logout_pending";
      accounts_.emplace(account.uuid, std::move(account));
      std::cerr << nlohmann::json{{"event", "account_awaiting_reconciliation"},
                                  {"account_uuid", manifest.uuid},
                                  {"reason", "reconciliation_not_received"}}
                       .dump()
                << '\n';
    }
    for (const auto &uuid : registry_.orphan_account_ids()) {
      orphan_accounts_.insert(uuid);
      std::cerr << nlohmann::json{{"event", "account_storage_unregistered"},
                                  {"account_uuid", uuid},
                                  {"reason", "reconciliation_not_received"}}
                       .dump()
                << '\n';
    }
    manifest_errors_ = registry_.manifest_errors();
    for (const auto &[uuid, error] : manifest_errors_) {
      std::cerr
          << nlohmann::json{{"event", "account_storage_error"}, {"account_uuid", uuid}, {"error_code", error}}.dump()
          << '\n';
    }
    status_.account_count = accounts_.size();
  }
  stopping_.store(false);
  receive_done_.store(false);
  receive_thread_ = std::thread(&TdRuntime::receive_loop, this);
}

void TdRuntime::stop() {
  if (!receive_thread_.joinable())
    return;
  stopping_.store(true);
  std::vector<std::int32_t> clients;
  {
    std::lock_guard lock(mutex_);
    status_.ready = false;
    for (const auto &[uuid, account] : accounts_) {
      static_cast<void>(uuid);
      if (account.client_id != 0 && !account.closed)
        clients.push_back(account.client_id);
    }
    for (auto &[request_id, pending] : pending_) {
      static_cast<void>(request_id);
      pending.promise.set_value(td_api::make_object<td_api::error>(503, "service.stopping"));
    }
    pending_.clear();
  }
  for (const auto client : clients) {
    transport_->send(client, next_id(), td_api::make_object<td_api::close>());
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  {
    std::unique_lock lock(mutex_);
    condition_.wait_until(lock, deadline, [this] {
      return std::none_of(accounts_.begin(), accounts_.end(),
                          [](const auto &entry) { return entry.second.client_id != 0 && !entry.second.closed; });
    });
  }
  if (std::chrono::steady_clock::now() >= deadline) {
    std::cerr << nlohmann::json{{"event", "runtime_shutdown_timeout"}}.dump() << '\n';
  }
  receive_done_.store(true);
  receive_thread_.join();
}

StatusSnapshot TdRuntime::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

nlohmann::json TdRuntime::account_json(const Account &account) const {
  const auto resend_after =
      account.resend_available_at == std::chrono::steady_clock::time_point{}
          ? 0
          : std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::seconds>(account.resend_available_at -
                                                                                       std::chrono::steady_clock::now())
                                          .count());
  const bool resend_state =
      account.authorization_state == "awaiting_code" || account.authorization_state == "awaiting_email_code";
  nlohmann::json authorization{{"state", account.authorization_state},
                               {"authorization_version", account.authorization_version},
                               {"allowed_actions", allowed_actions(account)},
                               {"resend_available", resend_state && account.delivery_supported && resend_after == 0},
                               {"resend_after_seconds", resend_after}};
  if (!account.delivery_method.empty()) {
    authorization["delivery_method"] = account.delivery_method;
  }
  if (account.authorization_state == "awaiting_qr_confirmation" && !account.qr_link.empty()) {
    authorization["qr_link"] = account.qr_link;
  }
  nlohmann::json result{
      {"uuid", account.uuid},
      {"generation", account.generation},
      {"applied_revision", account.revision},
      {"runtime_available", account.reconciled && !account.closed},
      {"authorization_state", account.authorization_state},
      {"connection_state", account.connection_state},
      {"authorization", authorization},
      {"operation_id", account.operation_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(account.operation_id)},
      {"last_error_code", account.last_error.empty() ? nlohmann::json(nullptr) : nlohmann::json(account.last_error)}};
  if (!account.telegram_identity.is_null()) {
    result["telegram_identity"] = account.telegram_identity;
  }
  return result;
}

nlohmann::json TdRuntime::snapshots(const std::vector<std::string> &ids) const {
  std::lock_guard lock(mutex_);
  nlohmann::json result = nlohmann::json::object();
  if (ids.empty()) {
    for (const auto &[id, account] : accounts_) {
      result[id] = account_json(account);
    }
  } else {
    for (const auto &id : ids) {
      if (const auto iterator = accounts_.find(id); iterator != accounts_.end())
        result[id] = account_json(iterator->second);
    }
  }
  return {{"accounts", result}, {"unregistered_storage", orphan_accounts_}, {"manifest_errors", manifest_errors_}};
}

nlohmann::json TdRuntime::snapshot(const std::string &uuid) const {
  std::lock_guard lock(mutex_);
  const auto iterator = accounts_.find(uuid);
  return iterator == accounts_.end() ? safe_error("account.not_found", 404) : account_json(iterator->second);
}

TdRuntime::Account &TdRuntime::validated_account(const std::string &uuid, const nlohmann::json &command) {
  if (!valid_uuid(uuid) || command.value("uuid", "") != uuid || !valid_uuid(command.value("generation", "")) ||
      !command.contains("revision") || !command["revision"].is_number_integer() ||
      command["revision"].get<std::int64_t>() < 1) {
    throw std::runtime_error("request.invalid");
  }
  if (manifest_errors_.contains(uuid))
    throw std::runtime_error(manifest_errors_.at(uuid));
  if (orphan_accounts_.contains(uuid) && command.value("lifecycle", "") != "provisioning")
    throw std::runtime_error("storage.corrupt");
  auto iterator = accounts_.find(uuid);
  if (iterator == accounts_.end()) {
    Account account;
    account.uuid = uuid;
    account.generation = command.at("generation").get<std::string>();
    account.use_test_dc = config_.use_test_dc;
    iterator = accounts_.emplace(uuid, std::move(account)).first;
    status_.account_count = accounts_.size();
  }
  auto &account = iterator->second;
  if (account.use_test_dc != config_.use_test_dc)
    throw std::runtime_error("configuration.environment_mismatch");
  if (account.generation != command.at("generation").get<std::string>())
    throw std::runtime_error("storage.identity_mismatch");
  const auto revision = command.at("revision").get<std::uint64_t>();
  if (revision < account.revision)
    throw std::runtime_error("operation.conflict");
  if (account.tombstone)
    throw std::runtime_error("account.gone");
  return account;
}

nlohmann::json TdRuntime::reconcile(const std::string &uuid, const nlohmann::json &command) {
  Account *account = nullptr;
  const std::string fingerprint = command_fingerprint(command);
  {
    std::lock_guard lock(mutex_);
    account = &validated_account(uuid, command);
    const auto revision = command.at("revision").get<std::uint64_t>();
    if (revision == account->revision && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      if (account->reconciled) {
        auto result = account_json(*account);
        result["completed"] = account->operation_id.empty();
        return result;
      }
    }
    if (command.value("mode", "") == "restore" && !registry_.account_directory_exists(uuid))
      return safe_error("storage.missing", 409);
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    account->revision = revision;
    account->lifecycle = command.value("lifecycle", "provisioning");
    const auto requested_proxy = command.value("proxy", nlohmann::json::object());
    if (requested_proxy.size() == 1 && requested_proxy.contains("id")) {
      if (account->proxy.value("id", "") != requested_proxy.value("id", ""))
        return safe_error("configuration.missing", 409);
    } else {
      account->proxy = requested_proxy;
    }
    account->operation_id = nullable_string(command, "operation_id");
    account->revision_fingerprint = fingerprint;
  }
  try {
    registry_.ensure_account_directories(uuid);
    AccountManifest manifest{1,
                             uuid,
                             account->generation,
                             config_.use_test_dc,
                             1,
                             account->revision,
                             account->lifecycle,
                             account->operation_id,
                             false,
                             nullptr,
                             ""};
    manifest.content_hash = account->revision_fingerprint;
    registry_.write(manifest, account->proxy);
    {
      std::lock_guard lock(mutex_);
      orphan_accounts_.erase(uuid);
    }
    activate(*account);
    nlohmann::json proxy;
    AccountManifest completed;
    {
      std::lock_guard lock(mutex_);
      account->reconciled = true;
      account->busy = false;
      account->lifecycle = "active";
      account->last_error.clear();
      completed = manifest;
      completed.lifecycle = "active";
      proxy = account->proxy;
    }
    registry_.write(completed, proxy);
    std::lock_guard lock(mutex_);
    const auto waited =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - account->discovered_at)
            .count();
    std::cerr << nlohmann::json{{"event", "account_reconciled"}, {"account_uuid", uuid}, {"awaited_ms", waited}}.dump()
              << '\n';
    return account_json(*account);
  } catch (const std::exception &exception) {
    std::lock_guard lock(mutex_);
    const std::string detail = exception.what();
    account->busy = false;
    if (detail == "operation.outcome_unknown") {
      account->last_error = detail;
      return safe_error(detail, 504);
    }
    account->last_error = detail.starts_with("storage.") || detail.starts_with("configuration.") ||
                                  detail.starts_with("service.") || detail.starts_with("telegram.")
                              ? detail
                              : "configuration.invalid";
    return safe_error(account->last_error, account->last_error.starts_with("service.") ? 503 : 409);
  }
}

void TdRuntime::activate(Account &account) {
  std::string uuid;
  {
    std::lock_guard lock(mutex_);
    if (account.client_id != 0 && !account.closed)
      return;
    uuid = account.uuid;
  }
  if (config_.telegram_api_id == 0 || config_.telegram_api_hash.empty() || config_.master_key_file.empty()) {
    throw std::runtime_error("configuration.missing");
  }
  if (!std::filesystem::exists(config_.master_key_file))
    throw std::runtime_error("configuration.missing");
  std::array<std::uint8_t, 32> master_key{};
  try {
    master_key = read_master_key(config_.master_key_file);
  } catch (const std::exception &) {
    throw std::runtime_error("storage.invalid_key");
  }
  const auto derived = derive_database_key(master_key, uuid);
  const auto client = transport_->create_client_id();
  {
    std::lock_guard lock(mutex_);
    account.client_id = client;
    account.closed = false;
    client_accounts_[client] = uuid;
  }
  try {
    const auto root = registry_.root() / "accounts" / uuid;
    auto network_barrier = begin_request(
        client, td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeNone>()));
    const std::string key(reinterpret_cast<const char *>(derived.data()), derived.size());
    auto parameters =
        begin_request(client, td_api::make_object<td_api::setTdlibParameters>(
                                  config_.use_test_dc, (root / "db").string(), (root / "files").string(), key, false,
                                  false, false, false, config_.telegram_api_id, config_.telegram_api_hash, "en",
                                  "TeleBezel", "Linux", TELEBEZEL_SERVICE_VERSION));
    auto response = await_request(std::move(network_barrier), std::chrono::seconds(8));
    auto parameters_response = await_request(std::move(parameters), std::chrono::seconds(8));
    throw_runtime_control_error(response);
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
    response = std::move(parameters_response);
    throw_runtime_control_error(response);
    if (td_error_code(response, 401))
      throw std::runtime_error("storage.invalid_key");
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
    apply_proxy(account);
    response =
        request(client, td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeOther>()));
    throw_runtime_control_error(response);
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
  } catch (...) {
    transport_->send(client, next_id(), td_api::make_object<td_api::close>());
    std::lock_guard lock(mutex_);
    client_accounts_.erase(client);
    if (account.client_id == client) {
      account.client_id = 0;
      account.closed = true;
    }
    throw;
  }
}

void TdRuntime::apply_proxy(Account &account) {
  std::int32_t client = 0;
  nlohmann::json desired;
  {
    std::lock_guard lock(mutex_);
    client = account.client_id;
    desired = account.proxy;
  }
  const auto disabled = request(client, td_api::make_object<td_api::disableProxy>());
  throw_runtime_control_error(disabled);
  if (!disabled || disabled->get_id() == td_api::error::ID)
    throw std::runtime_error("configuration.invalid");
  auto existing = request(client, td_api::make_object<td_api::getProxies>());
  if (existing && existing->get_id() == td_api::addedProxies::ID) {
    auto proxies = td::move_tl_object_as<td_api::addedProxies>(existing);
    for (const auto &proxy : proxies->proxies_) {
      if (proxy && proxy->comment_ == "telebezel-managed") {
        const auto removed = request(client, td_api::make_object<td_api::removeProxy>(proxy->id_));
        throw_runtime_control_error(removed);
        if (!removed || removed->get_id() == td_api::error::ID)
          throw std::runtime_error("configuration.invalid");
      }
    }
  }
  const std::string mode = desired.value("mode", "inherit");
  if (mode != "inherit" && !valid_uuid(desired.value("id", "")))
    throw std::runtime_error("configuration.invalid");
  const ProxyConfig *selected = &config_.proxy;
  ProxyConfig override;
  if (mode != "inherit") {
    if (mode == "direct")
      selected = nullptr;
    else {
      override.mode = parse_proxy_mode(mode);
      override.host = desired.value("host", "");
      override.port = desired.value("port", 0);
      override.http_only = desired.value("http_only", false);
      override.username = desired.value("username", "");
      override.password = desired.value("password", "");
      override.secret = desired.value("secret", "");
      selected = &override;
    }
  }
  if (selected == nullptr || selected->mode == ProxyMode::direct || selected->mode == ProxyMode::inherit) {
    return;
  }
  if (selected->host.empty() || selected->port == 0 ||
      (selected->mode == ProxyMode::mtproto &&
       (selected->secret.empty() || !selected->username.empty() || !selected->password.empty())) ||
      ((selected->mode == ProxyMode::socks5 || selected->mode == ProxyMode::http) && !selected->secret.empty()) ||
      (!selected->password.empty() && selected->username.empty()) ||
      (selected->mode != ProxyMode::http && selected->http_only)) {
    throw std::runtime_error("configuration.invalid");
  }
  td_api::object_ptr<td_api::ProxyType> type;
  if (selected->mode == ProxyMode::socks5)
    type = td_api::make_object<td_api::proxyTypeSocks5>(selected->username, selected->password);
  else if (selected->mode == ProxyMode::http)
    type = td_api::make_object<td_api::proxyTypeHttp>(selected->username, selected->password, selected->http_only);
  else
    type = td_api::make_object<td_api::proxyTypeMtproto>(selected->secret);
  auto proxy = td_api::make_object<td_api::proxy>(selected->host, selected->port, std::move(type));
  const auto response =
      request(client, td_api::make_object<td_api::addProxy>(std::move(proxy), true, "telebezel-managed"));
  throw_runtime_control_error(response);
  if (!response || response->get_id() != td_api::addedProxy::ID)
    throw std::runtime_error("configuration.invalid");
}

std::uint64_t TdRuntime::next_id() {
  auto value = next_request_id_.fetch_add(1);
  if (value == 0)
    value = next_request_id_.fetch_add(1);
  return value;
}

td_api::object_ptr<td_api::Object> TdRuntime::request(std::int32_t client_id,
                                                      td_api::object_ptr<td_api::Function> function,
                                                      std::chrono::seconds timeout, bool recover_stalled_client) {
  return await_request(begin_request(client_id, std::move(function)), timeout, recover_stalled_client);
}

TdRuntime::RequestHandle TdRuntime::begin_request(std::int32_t client_id,
                                                  td_api::object_ptr<td_api::Function> function) {
  std::promise<td_api::object_ptr<td_api::Object>> immediate;
  auto immediate_future = immediate.get_future();
  const bool stopping = stopping_.load();
  if (stopping)
    immediate.set_value(td_api::make_object<td_api::error>(503, "service.stopping"));
  if (stopping)
    return {0, client_id, std::move(immediate_future), false};
  const auto request_id = next_id();
  std::future<td_api::object_ptr<td_api::Object>> future;
  {
    std::lock_guard lock(mutex_);
    if (pending_.size() + async_requests_.size() >= 1024) {
      immediate.set_value(td_api::make_object<td_api::error>(503, "service.busy"));
      return {0, client_id, std::move(immediate_future), false};
    }
    Pending pending{client_id, {}};
    future = pending.promise.get_future();
    pending_.emplace(request_id, std::move(pending));
  }
  transport_->send(client_id, request_id, std::move(function));
  return {request_id, client_id, std::move(future), true};
}

td_api::object_ptr<td_api::Object> TdRuntime::await_request(RequestHandle handle, std::chrono::seconds timeout,
                                                            bool recover_stalled_client) {
  if (handle.future.wait_for(timeout) != std::future_status::ready) {
    std::lock_guard lock(mutex_);
    pending_.erase(handle.request_id);
    if (unresolved_.size() >= 1024)
      unresolved_.erase(unresolved_.begin());
    if (handle.pending) {
      unresolved_[handle.request_id] = {handle.client_id, std::chrono::steady_clock::now() + std::chrono::seconds(22),
                                        recover_stalled_client};
    }
    return td_api::make_object<td_api::error>(504, "operation.outcome_unknown");
  }
  return handle.future.get();
}

void TdRuntime::request_identity(Account &account) {
  if (account.client_id == 0 || async_requests_.size() + pending_.size() >= 1024) {
    if (account.client_id != 0)
      account.last_error = "service.busy";
    return;
  }
  const auto request_id = next_id();
  async_requests_.emplace(request_id, AsyncRequest{account.client_id, AsyncRequestKind::get_me,
                                                   std::chrono::steady_clock::now() + std::chrono::seconds(8)});
  transport_->send(account.client_id, request_id, td_api::make_object<td_api::getMe>());
}

void TdRuntime::receive_loop() {
  auto next_reminder = std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (!receive_done_.load()) {
    std::vector<std::int32_t> stalled_clients;
    {
      std::lock_guard lock(mutex_);
      const auto now = std::chrono::steady_clock::now();
      for (auto &[request_id, unresolved] : unresolved_) {
        static_cast<void>(request_id);
        if (unresolved.recover_client && unresolved.recovery_at <= now) {
          stalled_clients.push_back(unresolved.client_id);
          unresolved.recover_client = false;
        }
      }
      for (auto iterator = async_requests_.begin(); iterator != async_requests_.end();) {
        if (iterator->second.deadline <= now)
          iterator = async_requests_.erase(iterator);
        else
          ++iterator;
      }
    }
    for (const auto client : stalled_clients) {
      transport_->send(client, next_id(), td_api::make_object<td_api::close>());
    }
    auto response = transport_->receive(0.1);
    if (!response.object) {
      if (std::chrono::steady_clock::now() >= next_reminder) {
        std::lock_guard lock(mutex_);
        for (const auto &[uuid, account] : accounts_) {
          if (!account.reconciled && !account.closed) {
            std::cerr << nlohmann::json{{"event", "account_awaiting_reconciliation"},
                                        {"account_uuid", uuid},
                                        {"reason", "reconciliation_not_received"}}
                             .dump()
                      << '\n';
          }
        }
        next_reminder = std::chrono::steady_clock::now() + std::chrono::minutes(5);
      }
      continue;
    }
    std::lock_guard lock(mutex_);
    if (response.request_id == 0) {
      handle_update(response.client_id, *response.object);
      continue;
    }
    const auto asynchronous = async_requests_.find(response.request_id);
    if (asynchronous != async_requests_.end()) {
      if (asynchronous->second.client_id == response.client_id &&
          asynchronous->second.kind == AsyncRequestKind::get_me && response.object->get_id() == td_api::user::ID) {
        const auto mapped = client_accounts_.find(response.client_id);
        if (mapped != client_accounts_.end()) {
          const auto &user = static_cast<const td_api::user &>(*response.object);
          std::vector<std::string> usernames;
          if (user.usernames_)
            usernames = user.usernames_->active_usernames_;
          accounts_.at(mapped->second).telegram_identity = {{"id", std::to_string(user.id_)},
                                                            {"first_name", user.first_name_},
                                                            {"last_name", user.last_name_},
                                                            {"usernames", usernames},
                                                            {"is_premium", user.is_premium_}};
        }
      }
      async_requests_.erase(asynchronous);
      continue;
    }
    const auto pending = pending_.find(response.request_id);
    if (pending == pending_.end()) {
      const auto unresolved = unresolved_.find(response.request_id);
      if (unresolved != unresolved_.end() && unresolved->second.client_id == response.client_id) {
        unresolved_.erase(unresolved);
        std::cerr << nlohmann::json{{"event", "tdlib_late_response"}, {"request_id", response.request_id}}.dump()
                  << '\n';
      }
      continue;
    }
    if (pending->second.client_id != response.client_id)
      continue;
    pending->second.promise.set_value(std::move(response.object));
    pending_.erase(pending);
  }
}

void TdRuntime::handle_update(std::int32_t client_id, td_api::Object &object) {
  const auto mapped = client_accounts_.find(client_id);
  if (mapped == client_accounts_.end())
    return;
  auto &account = accounts_.at(mapped->second);
  if (object.get_id() == td_api::updateAuthorizationState::ID) {
    auto &update = static_cast<td_api::updateAuthorizationState &>(object);
    const auto state_id = update.authorization_state_ ? update.authorization_state_->get_id() : 0;
    account.authorization_state = authorization_name(state_id);
    account.authorization_version = std::to_string(next_id());
    account.qr_link.clear();
    account.delivery_method.clear();
    account.delivery_supported = true;
    account.resend_available_at = {};
    if (account.authorization_state == "error") {
      account.last_error = "authorization.unsupported_state";
    } else if (account.last_error == "authorization.unsupported_state" ||
               account.last_error == "authorization.unsupported_delivery") {
      account.last_error.clear();
    }
    if (state_id == td_api::authorizationStateWaitCode::ID) {
      const auto &state = static_cast<td_api::authorizationStateWaitCode &>(*update.authorization_state_);
      if (state.code_info_) {
        const auto [method, supported] = delivery_method(state.code_info_->type_.get());
        account.delivery_method = method;
        account.delivery_supported = supported;
        account.resend_available_at =
            std::chrono::steady_clock::now() + std::chrono::seconds(std::max(0, state.code_info_->timeout_));
        if (!supported)
          account.last_error = "authorization.unsupported_delivery";
      }
    } else if (state_id == td_api::authorizationStateWaitEmailCode::ID) {
      account.delivery_method = "email";
    }
    if (state_id == td_api::authorizationStateWaitOtherDeviceConfirmation::ID) {
      account.qr_link =
          static_cast<td_api::authorizationStateWaitOtherDeviceConfirmation &>(*update.authorization_state_).link_;
    }
    if (state_id == td_api::authorizationStateClosed::ID) {
      account.closed = true;
      if (account.client_id == client_id)
        account.client_id = 0;
      client_accounts_.erase(mapped);
      if (account.lifecycle != "logout_pending" && account.lifecycle != "removing")
        account.reconciled = false;
      condition_.notify_all();
    } else if (state_id == td_api::authorizationStateReady::ID) {
      request_identity(account);
    }
  } else if (object.get_id() == td_api::updateConnectionState::ID) {
    auto &update = static_cast<td_api::updateConnectionState &>(object);
    account.connection_state = update.state_ ? connection_name(update.state_->get_id()) : "unknown";
  }
}

nlohmann::json TdRuntime::authorization_action(const std::string &uuid, const nlohmann::json &command) {
  std::int32_t client = 0;
  std::string action;
  std::string state;
  {
    std::lock_guard lock(mutex_);
    auto &account = validated_account(uuid, command);
    action = command.value("action", "");
    const bool needs_value = action != "start_qr" && action != "resend_code";
    if ((needs_value && (!command.contains("value") || !command["value"].is_string() ||
                         command["value"].get_ref<const std::string &>().empty())) ||
        (!needs_value && command.contains("value"))) {
      throw std::runtime_error("request.invalid");
    }
    const auto actions = allowed_actions(account);
    if (!account.reconciled || command.value("authorization_version", "") != account.authorization_version ||
        std::find(actions.begin(), actions.end(), action) == actions.end()) {
      return safe_error("authorization.invalid_state", 409);
    }
    if (account.busy)
      return safe_error("operation.conflict", 409);
    account.busy = true;
    client = account.client_id;
    state = account.authorization_state;
  }
  td_api::object_ptr<td_api::Function> function;
  const std::string value = command.value("value", "");
  if (action == "submit_phone_number") {
    function = td_api::make_object<td_api::setAuthenticationPhoneNumber>(
        value, td_api::make_object<td_api::phoneNumberAuthenticationSettings>());
  } else if (action == "submit_code")
    function = td_api::make_object<td_api::checkAuthenticationCode>(value);
  else if (action == "submit_password")
    function = td_api::make_object<td_api::checkAuthenticationPassword>(value);
  else if (action == "submit_email_address")
    function = td_api::make_object<td_api::setAuthenticationEmailAddress>(value);
  else if (action == "submit_email_code") {
    function = td_api::make_object<td_api::checkAuthenticationEmailCode>(
        td_api::make_object<td_api::emailAddressAuthenticationCode>(value));
  } else if (action == "start_qr") {
    function = td_api::make_object<td_api::requestQrCodeAuthentication>(std::vector<std::int64_t>{});
  } else if (state == "awaiting_email_code") {
    function = td_api::make_object<td_api::resendLoginEmailAddressCode>();
  } else {
    function = td_api::make_object<td_api::resendAuthenticationCode>(
        td_api::make_object<td_api::resendCodeReasonUserRequest>());
  }
  auto response = request(client, std::move(function), std::chrono::seconds(8), true);
  std::lock_guard lock(mutex_);
  auto &account = accounts_.at(uuid);
  account.busy = false;
  if (!response || response->get_id() == td_api::error::ID) {
    const int code =
        response && response->get_id() == td_api::error::ID ? static_cast<td_api::error &>(*response).code_ : 500;
    if (code == 504)
      return safe_error("operation.outcome_unknown", 504);
    const std::string message = response && response->get_id() == td_api::error::ID
                                    ? static_cast<td_api::error &>(*response).message_
                                    : std::string{};
    const std::string error =
        code == 429 || message.find("FLOOD_WAIT") != std::string::npos ? "authorization.flood_wait"
        : message.find("CODE_EXPIRED") != std::string::npos            ? "authorization.code_expired"
        : message.find("CODE_INVALID") != std::string::npos            ? "authorization.invalid_code"
        : message.find("PASSWORD_HASH_INVALID") != std::string::npos   ? "authorization.invalid_password"
        : action == "submit_code" || action == "submit_email_code"     ? "authorization.invalid_code"
        : action == "submit_password"                                  ? "authorization.invalid_password"
                                                                       : "telegram.operation_failed";
    return safe_error(error, code == 429 ? 429 : 422);
  }
  return account_json(account);
}

nlohmann::json TdRuntime::logout(const std::string &uuid, const nlohmann::json &command) {
  std::int32_t client = 0;
  Account *selected = nullptr;
  bool resume_closed = false;
  bool persist_intent = false;
  AccountManifest intent_manifest;
  nlohmann::json intent_proxy;
  const std::string fingerprint = command_fingerprint(command);
  {
    std::lock_guard lock(mutex_);
    auto &account = validated_account(uuid, command);
    if (account.revision == command.at("revision").get<std::uint64_t>() && !account.revision_fingerprint.empty() &&
        account.revision_fingerprint != fingerprint)
      return safe_error("operation.conflict", 409);
    if (account.operation_id == nullable_string(command, "logout_operation_id") &&
        account.lifecycle == "logout_pending") {
      if (!account.closed)
        return account_json(account);
      if (account.busy)
        return safe_error("operation.conflict", 409);
      account.busy = true;
      selected = &account;
      resume_closed = true;
    } else {
      if (account.busy)
        return safe_error("operation.conflict", 409);
      account.busy = true;
      account.revision = command.at("revision").get<std::uint64_t>();
      account.lifecycle = "logout_pending";
      account.operation_id = nullable_string(command, "logout_operation_id");
      account.revision_fingerprint = fingerprint;
      client = account.client_id;
      selected = &account;
      intent_manifest = {1,
                         uuid,
                         account.generation,
                         config_.use_test_dc,
                         1,
                         account.revision,
                         account.lifecycle,
                         account.operation_id,
                         false,
                         nullptr,
                         ""};
      intent_manifest.content_hash = account.revision_fingerprint;
      intent_proxy = account.proxy;
      persist_intent = true;
    }
  }
  if (persist_intent) {
    try {
      registry_.write(intent_manifest, intent_proxy);
    } catch (...) {
      std::lock_guard lock(mutex_);
      selected->busy = false;
      throw;
    }
  }
  if (resume_closed) {
    try {
      activate(*selected);
    } catch (...) {
      std::lock_guard lock(mutex_);
      selected->busy = false;
      throw;
    }
    AccountManifest manifest;
    nlohmann::json proxy;
    std::uint64_t revision = 0;
    {
      std::lock_guard lock(mutex_);
      selected->lifecycle = "active";
      selected->operation_id.clear();
      selected->reconciled = true;
      selected->busy = false;
      manifest = {1,       uuid, selected->generation, config_.use_test_dc, 1, selected->revision, "active", "", false,
                  nullptr, ""};
      manifest.content_hash = selected->revision_fingerprint;
      proxy = selected->proxy;
      revision = selected->revision;
    }
    registry_.write(manifest, proxy);
    return {{"applied_revision", revision}, {"completed", true}};
  }
  const auto response = request(client, td_api::make_object<td_api::logOut>());
  if (td_error_code(response, 504)) {
    std::lock_guard lock(mutex_);
    accounts_.at(uuid).busy = false;
    return safe_error("operation.outcome_unknown", 504);
  }
  if (!response || response->get_id() == td_api::error::ID) {
    std::lock_guard lock(mutex_);
    accounts_.at(uuid).busy = false;
    return safe_error(td_error_message(response, "service.busy") ? "service.busy" : "telegram.operation_failed",
                      td_error_message(response, "service.busy") ? 503 : 502);
  }
  std::lock_guard lock(mutex_);
  accounts_.at(uuid).busy = false;
  return {{"applied_revision", accounts_.at(uuid).revision}, {"completed", false}};
}

nlohmann::json TdRuntime::update_proxy(const std::string &uuid, const nlohmann::json &command) {
  Account *account = nullptr;
  const std::string fingerprint = command_fingerprint(command);
  AccountManifest intent_manifest;
  nlohmann::json intent_proxy;
  {
    std::lock_guard lock(mutex_);
    account = &validated_account(uuid, command);
    if (account->revision == command.at("revision").get<std::uint64_t>() && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      auto result = account_json(*account);
      result["completed"] = account->operation_id.empty();
      return result;
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    account->revision = command.at("revision").get<std::uint64_t>();
    account->proxy = command.value("proxy", nlohmann::json::object());
    account->operation_id = nullable_string(command, "operation_id");
    account->revision_fingerprint = fingerprint;
    intent_manifest = {1,
                       uuid,
                       account->generation,
                       config_.use_test_dc,
                       1,
                       account->revision,
                       account->lifecycle,
                       account->operation_id,
                       false,
                       nullptr,
                       ""};
    intent_manifest.content_hash = account->revision_fingerprint;
    intent_proxy = account->proxy;
  }
  try {
    registry_.write(intent_manifest, intent_proxy);
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  try {
    close_account(*account, false);
    activate(*account);
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  AccountManifest completed;
  nlohmann::json proxy;
  nlohmann::json result;
  {
    std::lock_guard lock(mutex_);
    account->reconciled = true;
    account->busy = false;
    account->operation_id.clear();
    completed = intent_manifest;
    completed.operation_id.clear();
    proxy = account->proxy;
    result = account_json(*account);
  }
  registry_.write(completed, proxy);
  result["completed"] = true;
  return result;
}

nlohmann::json TdRuntime::remove(const std::string &uuid, const nlohmann::json &command) {
  Account *account = nullptr;
  const std::string fingerprint = command_fingerprint(command);
  AccountManifest intent_manifest;
  nlohmann::json intent_proxy;
  {
    std::lock_guard lock(mutex_);
    if (const auto existing = accounts_.find(uuid); existing != accounts_.end() && existing->second.tombstone) {
      const auto &removed = existing->second;
      if (removed.lifecycle == "removed" && command.value("generation", "") == removed.generation &&
          command.value("revision", 0ULL) == removed.revision && removed.revision_fingerprint == fingerprint) {
        return {{"applied_revision", removed.revision}, {"completed", true}};
      }
      return safe_error("account.gone", 410);
    }
    account = &validated_account(uuid, command);
    if (account->revision == command.at("revision").get<std::uint64_t>() && !account->revision_fingerprint.empty()) {
      return account->revision_fingerprint == fingerprint && account->lifecycle == "removed"
                 ? nlohmann::json{{"applied_revision", account->revision}, {"completed", true}}
                 : safe_error("operation.conflict", 409);
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    account->revision = command.at("revision").get<std::uint64_t>();
    account->lifecycle = "removing";
    account->operation_id = nullable_string(command, "operation_id");
    account->revision_fingerprint = fingerprint;
    intent_manifest = {1,
                       uuid,
                       account->generation,
                       config_.use_test_dc,
                       1,
                       account->revision,
                       account->lifecycle,
                       account->operation_id,
                       false,
                       nullptr,
                       ""};
    intent_manifest.content_hash = account->revision_fingerprint;
    intent_proxy = account->proxy;
  }
  try {
    registry_.write(intent_manifest, intent_proxy);
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  try {
    close_account(*account, true);
    registry_.remove_account_directory(uuid);
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  AccountManifest tombstone{1,
                            uuid,
                            account->generation,
                            config_.use_test_dc,
                            1,
                            account->revision,
                            "removed",
                            account->operation_id,
                            true,
                            nullptr,
                            ""};
  tombstone.content_hash = account->revision_fingerprint;
  try {
    registry_.write(tombstone, nlohmann::json{{"mode", "inherit"}, {"http_only", false}});
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  std::lock_guard lock(mutex_);
  account->closed = true;
  account->tombstone = true;
  account->reconciled = false;
  account->busy = false;
  account->lifecycle = "removed";
  return {{"applied_revision", account->revision}, {"completed", true}};
}

void TdRuntime::close_account(Account &account, bool destroy) {
  std::int32_t client = 0;
  {
    std::lock_guard lock(mutex_);
    if (account.client_id == 0 || account.closed)
      return;
    client = account.client_id;
  }
  td_api::object_ptr<td_api::Function> function =
      destroy ? td_api::object_ptr<td_api::Function>(td_api::make_object<td_api::destroy>())
              : td_api::object_ptr<td_api::Function>(td_api::make_object<td_api::close>());
  const auto response = request(client, std::move(function));
  if (td_error_code(response, 504))
    throw std::runtime_error("operation.outcome_unknown");
  if (!response || response->get_id() == td_api::error::ID)
    throw std::runtime_error("telegram.operation_failed");
  std::unique_lock lock(mutex_);
  if (!condition_.wait_for(lock, std::chrono::seconds(8), [&account] { return account.closed; }))
    throw std::runtime_error("operation.outcome_unknown");
  client_accounts_.erase(client);
  account.client_id = 0;
}

std::string TdRuntime::authorization_name(std::int32_t id) {
  if (id == td_api::authorizationStateWaitTdlibParameters::ID)
    return "initializing";
  if (id == td_api::authorizationStateWaitPhoneNumber::ID)
    return "awaiting_phone_number";
  if (id == td_api::authorizationStateWaitPremiumPurchase::ID)
    return "premium_purchase_required";
  if (id == td_api::authorizationStateWaitEmailAddress::ID)
    return "awaiting_email_address";
  if (id == td_api::authorizationStateWaitEmailCode::ID)
    return "awaiting_email_code";
  if (id == td_api::authorizationStateWaitCode::ID)
    return "awaiting_code";
  if (id == td_api::authorizationStateWaitOtherDeviceConfirmation::ID)
    return "awaiting_qr_confirmation";
  if (id == td_api::authorizationStateWaitRegistration::ID)
    return "registration_required";
  if (id == td_api::authorizationStateWaitPassword::ID)
    return "awaiting_password";
  if (id == td_api::authorizationStateReady::ID)
    return "ready";
  if (id == td_api::authorizationStateLoggingOut::ID)
    return "logging_out";
  if (id == td_api::authorizationStateClosing::ID)
    return "closing";
  if (id == td_api::authorizationStateClosed::ID)
    return "closed";
  return "error";
}

std::string TdRuntime::connection_name(std::int32_t id) {
  if (id == td_api::connectionStateWaitingForNetwork::ID)
    return "waiting_for_network";
  if (id == td_api::connectionStateConnectingToProxy::ID)
    return "connecting_to_proxy";
  if (id == td_api::connectionStateConnecting::ID)
    return "connecting";
  if (id == td_api::connectionStateUpdating::ID)
    return "updating";
  if (id == td_api::connectionStateReady::ID)
    return "ready";
  return "unknown";
}

std::vector<std::string> TdRuntime::allowed_actions(const Account &account) {
  const auto &state = account.authorization_state;
  if (state == "awaiting_phone_number")
    return {"submit_phone_number", "start_qr"};
  if (state == "awaiting_code") {
    std::vector<std::string> actions{"submit_code", "submit_phone_number", "start_qr"};
    if (account.delivery_supported && std::chrono::steady_clock::now() >= account.resend_available_at)
      actions.push_back("resend_code");
    return actions;
  }
  if (state == "awaiting_password")
    return {"submit_password"};
  if (state == "awaiting_email_address")
    return {"submit_email_address"};
  if (state == "awaiting_email_code")
    return {"submit_email_code", "resend_code"};
  if (state == "awaiting_qr_confirmation")
    return {"start_qr", "submit_phone_number"};
  return {};
}

nlohmann::json TdRuntime::safe_error(const std::string &code, int status) {
  return {{"_error", true}, {"code", code}, {"status", status}};
}
} // namespace telebezel
