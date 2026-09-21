#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
AccountState &AccountLifecycleService::validated_account(const std::string &uuid, const AccountCommand &command) {
  if (context_.manifest_errors_.contains(uuid))
    throw std::runtime_error(context_.manifest_errors_.at(uuid));
  if (context_.orphan_accounts_.contains(uuid) && command.lifecycle != "provisioning")
    throw std::runtime_error("storage.corrupt");
  auto iterator = context_.accounts_.find(uuid);
  if (iterator == context_.accounts_.end()) {
    Account account;
    account.uuid = uuid;
    account.generation = command.generation;
    account.use_test_dc = config_.use_test_dc;
    account.runtime_epoch = make_request_id();
    iterator = context_.accounts_.emplace(uuid, std::move(account)).first;
    context_.status_.account_count = context_.accounts_.size();
  }
  auto &account = iterator->second;
  if (account.use_test_dc != config_.use_test_dc)
    throw std::runtime_error("configuration.environment_mismatch");
  if (account.generation != command.generation)
    throw std::runtime_error("storage.identity_mismatch");
  const auto revision = command.revision;
  if (revision < account.revision)
    throw std::runtime_error("operation.conflict");
  const auto authorization_generation = command.authorization_generation.value_or(account.authorization_generation);
  if (authorization_generation < account.authorization_generation)
    throw std::runtime_error("operation.conflict");
  if (account.tombstone)
    throw std::runtime_error("account.gone");
  return account;
}

nlohmann::json AccountLifecycleService::reconcile(const std::string &uuid, const AccountCommand &command) {
  Account *account = nullptr;
  const std::string fingerprint = command.fingerprint;
  {
    std::lock_guard lock(context_.mutex_);
    account = &validated_account(uuid, command);
    if (account->busy)
      return safe_error("operation.conflict", 409);
    const auto revision = command.revision;
    if (revision == account->revision && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      if (account->reconciled) {
        auto result = account_json(*account);
        result["completed"] = account->operation_id.empty();
        return result;
      }
    }
    if (command.mode == ActivationMode::restore && !registry_.account_directory_exists(uuid))
      return safe_error("storage.missing", 409);
    if (account->busy)
      return safe_error("operation.conflict", 409);
    const auto requested_proxy = command.proxy;
    if (requested_proxy.size() == 1 && requested_proxy.contains("id") &&
        account->proxy.value("id", "") != requested_proxy.value("id", ""))
      return safe_error("configuration.missing", 409);
    account->busy = true;
    account->authorization_generation = command.authorization_generation.value_or(account->authorization_generation);
    account->revision = revision;
    account->effective_config_id = command.effective_config_id;
    account->telegram_api_id = command.telegram_api_id.value_or(config_.telegram_api_id);
    account->telegram_api_hash = command.telegram_api_hash.value_or(config_.telegram_api_hash);
    account->lifecycle = command.lifecycle;
    if (!(requested_proxy.size() == 1 && requested_proxy.contains("id"))) {
      account->proxy = requested_proxy;
    }
    account->operation_id = command.operation_id;
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
                             "",
                             ""};
    manifest.content_hash = account->revision_fingerprint;
    registry_.write(manifest, account->proxy);
    {
      std::lock_guard lock(context_.mutex_);
      context_.orphan_accounts_.erase(uuid);
    }
    activate(*account);
    nlohmann::json proxy;
    AccountManifest completed;
    {
      std::lock_guard lock(context_.mutex_);
      completed = manifest;
      completed.lifecycle = "active";
      proxy = account->proxy;
    }
    registry_.write(completed, proxy);
    std::lock_guard lock(context_.mutex_);
    account->reconciled = true;
    account->lifecycle = "active";
    account->last_error.clear();
    account->busy = false;
    const auto waited =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - account->discovered_at)
            .count();
    std::cerr << nlohmann::json{{"event", "account_reconciled"}, {"account_uuid", uuid}, {"awaited_ms", waited}}.dump()
              << '\n';
    return account_json(*account);
  } catch (const std::exception &exception) {
    std::lock_guard lock(context_.mutex_);
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

void AccountLifecycleService::activate(Account &account) {
  std::string uuid;
  const auto telegram_api_id = account.telegram_api_id == 0 ? config_.telegram_api_id : account.telegram_api_id;
  const auto &telegram_api_hash =
      account.telegram_api_hash.empty() ? config_.telegram_api_hash : account.telegram_api_hash;
  {
    std::lock_guard lock(context_.mutex_);
    if (account.client_id != 0 && account.closing)
      throw std::runtime_error("operation.outcome_unknown");
    if (account.client_id != 0 && !account.closed)
      return;
    uuid = account.uuid;
  }
  if (telegram_api_id == 0 || telegram_api_hash.empty() || config_.master_key_file.empty()) {
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
  const auto client = transport_.create_client_id();
  {
    std::lock_guard lock(context_.mutex_);
    account.client_id = client;
    account.closed = false;
    context_.client_accounts_[client] = uuid;
  }
  try {
    const auto root = registry_.root() / "accounts" / uuid;
    auto network_barrier = broker_.begin_request(
        client, td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeNone>()));
    const std::string key(reinterpret_cast<const char *>(derived.data()), derived.size());
    auto parameters = broker_.begin_request(
        client, td_api::make_object<td_api::setTdlibParameters>(
                    config_.use_test_dc, (root / "db").string(), (root / "files").string(), key, true, true, false,
                    false, telegram_api_id, telegram_api_hash, "en", "TeleBezel", "Linux", TELEBEZEL_SERVICE_VERSION));
    auto response = broker_.await_request(std::move(network_barrier), std::chrono::seconds(8));
    auto parameters_response = broker_.await_request(std::move(parameters), std::chrono::seconds(8));
    throw_runtime_control_error(response);
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
    response = std::move(parameters_response);
    throw_runtime_control_error(response);
    if (td_error_code(response, 401))
      throw std::runtime_error("storage.invalid_key");
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
    proxy_.apply_proxy(account);
    response = broker_.request(
        client, td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeOther>()));
    throw_runtime_control_error(response);
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
  } catch (...) {
    const auto failure = std::current_exception();
    {
      std::lock_guard lock(context_.mutex_);
      if (account.client_id == client)
        account.closing = true;
    }
    transport_.send(client, broker_.next_id(), td_api::make_object<td_api::close>());
    std::unique_lock lock(context_.mutex_);
    context_.condition_.wait_for(lock, std::chrono::seconds(8),
                                 [&account, client] { return account.client_id != client || account.closed; });
    lock.unlock();
    std::rethrow_exception(failure);
  }
}

nlohmann::json AccountLifecycleService::logout(const std::string &uuid, const AccountCommand &command) {
  Account *account = nullptr;
  bool new_operation = false;
  const std::string fingerprint = command.fingerprint;
  const std::string operation_id = command.logout_operation_id;
  {
    std::lock_guard lock(context_.mutex_);
    account = &validated_account(uuid, command);
    if (account->busy)
      return safe_error("operation.conflict", 409);
    const auto revision = command.revision;
    if (account->revision == revision && !account->revision_fingerprint.empty() &&
        account->revision_fingerprint != fingerprint)
      return safe_error("operation.conflict", 409);
    if (account->revision == revision && account->lifecycle == "logout_pending" &&
        account->operation_id != operation_id)
      return safe_error("operation.conflict", 409);
    const bool resuming = account->revision == revision && account->lifecycle == "logout_pending" &&
                          account->operation_id == operation_id;
    if (!resuming) {
      account->revision = revision;
      account->lifecycle = "logout_pending";
      account->operation_id = operation_id;
      account->operation_phase = "intent";
      account->revision_fingerprint = fingerprint;
      new_operation = true;
    } else {
      if (account->revision_fingerprint.empty())
        account->revision_fingerprint = fingerprint;
      if (account->operation_phase.empty()) {
        // Legacy manifests are an intent, never proof that logOut reached TDLib.
        account->operation_phase = "intent";
      }
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    account->authorization_generation = command.authorization_generation.value_or(account->authorization_generation);
  }

  const auto persist = [&](const std::string &phase, const std::string &lifecycle,
                           const std::string &persisted_operation) {
    AccountManifest manifest;
    nlohmann::json proxy;
    {
      std::lock_guard lock(context_.mutex_);
      account->operation_phase = phase;
      manifest = {1,
                  uuid,
                  account->generation,
                  config_.use_test_dc,
                  1,
                  account->revision,
                  lifecycle,
                  persisted_operation,
                  false,
                  nullptr,
                  "",
                  ""};
      manifest.content_hash = account->revision_fingerprint;
      manifest.operation_phase = phase;
      proxy = account->proxy;
    }
    registry_.write(manifest, proxy);
  };

  try {
    if (new_operation)
      persist("intent", "logout_pending", operation_id);

    bool closed = false;
    std::string phase;
    {
      std::lock_guard lock(context_.mutex_);
      closed = account->closed;
      phase = account->operation_phase;
    }
    if (closed && phase == "executing") {
      persist("confirmed", "logout_pending", operation_id);
      phase = "confirmed";
    }
    if (phase == "confirmed") {
      activate(*account);
      persist("", "active", "");
      std::lock_guard lock(context_.mutex_);
      account->lifecycle = "active";
      account->operation_id.clear();
      account->operation_phase.clear();
      account->reconciled = true;
      account->busy = false;
      return {{"applied_revision", account->revision}, {"completed", true}};
    }

    activate(*account);
    persist("executing", "logout_pending", operation_id);
    std::int32_t client = 0;
    {
      std::lock_guard lock(context_.mutex_);
      client = account->client_id;
    }
    const auto response = broker_.request(client, td_api::make_object<td_api::logOut>());
    if (td_error_code(response, 504)) {
      std::lock_guard lock(context_.mutex_);
      account->busy = false;
      return safe_error("operation.outcome_unknown", 504);
    }
    if (!response || response->get_id() == td_api::error::ID) {
      std::lock_guard lock(context_.mutex_);
      account->busy = false;
      return safe_error(td_error_message(response, "service.busy") ? "service.busy" : "telegram.operation_failed",
                        td_error_message(response, "service.busy") ? 503 : 502);
    }
    std::lock_guard lock(context_.mutex_);
    account->busy = false;
    return {{"applied_revision", account->revision}, {"completed", false}};
  } catch (...) {
    std::lock_guard lock(context_.mutex_);
    account->busy = false;
    throw;
  }
}

nlohmann::json AccountLifecycleService::update_proxy(const std::string &uuid, const AccountCommand &command) {
  Account *account = nullptr;
  const std::string fingerprint = command.fingerprint;
  AccountManifest intent_manifest;
  nlohmann::json intent_proxy;
  {
    std::lock_guard lock(context_.mutex_);
    account = &validated_account(uuid, command);
    if (account->busy)
      return safe_error("operation.conflict", 409);
    if (account->revision == command.revision && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      const auto requested_proxy = command.proxy;
      if (!(requested_proxy.size() == 1 && requested_proxy.contains("id")) && account->proxy != requested_proxy)
        return safe_error("operation.conflict", 409);
      auto result = account_json(*account);
      result["completed"] = account->operation_id.empty();
      return result;
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    account->authorization_generation = command.authorization_generation.value_or(account->authorization_generation);
    account->revision = command.revision;
    account->effective_config_id = command.effective_config_id;
    account->proxy = command.proxy;
    account->operation_id = command.operation_id;
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
                       "",
                       ""};
    intent_manifest.content_hash = account->revision_fingerprint;
    intent_proxy = account->proxy;
  }
  try {
    registry_.write(intent_manifest, intent_proxy);
  } catch (...) {
    std::lock_guard lock(context_.mutex_);
    account->busy = false;
    throw;
  }
  try {
    close_account(*account, false);
    activate(*account);
  } catch (...) {
    std::lock_guard lock(context_.mutex_);
    account->busy = false;
    throw;
  }
  AccountManifest completed = intent_manifest;
  completed.operation_id.clear();
  try {
    registry_.write(completed, intent_proxy);
  } catch (...) {
    std::lock_guard lock(context_.mutex_);
    account->busy = false;
    throw;
  }
  std::lock_guard lock(context_.mutex_);
  account->reconciled = true;
  account->busy = false;
  account->operation_id.clear();
  auto result = account_json(*account);
  result["completed"] = true;
  return result;
}

nlohmann::json AccountLifecycleService::remove(const std::string &uuid, const AccountCommand &command) {
  Account *account = nullptr;
  const std::string fingerprint = command.fingerprint;
  const std::string operation_id = command.operation_id;
  AccountManifest intent_manifest;
  nlohmann::json intent_proxy;
  bool new_operation = false;
  {
    std::lock_guard lock(context_.mutex_);
    if (const auto existing = context_.accounts_.find(uuid);
        existing != context_.accounts_.end() && existing->second.tombstone) {
      const auto &removed = existing->second;
      if (removed.lifecycle == "removed" && command.generation == removed.generation &&
          command.revision == removed.revision && removed.revision_fingerprint == fingerprint) {
        return {{"applied_revision", removed.revision}, {"completed", true}};
      }
      return safe_error("account.gone", 410);
    }
    account = &validated_account(uuid, command);
    if (account->busy)
      return safe_error("operation.conflict", 409);
    if (account->revision == command.revision && account->lifecycle == "removing" &&
        account->operation_id != operation_id)
      return safe_error("operation.conflict", 409);
    if (account->revision == command.revision && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      if (account->lifecycle == "removed")
        return {{"applied_revision", account->revision}, {"completed", true}};
      if (account->lifecycle != "removing" || account->operation_id != operation_id)
        return safe_error("operation.conflict", 409);
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    account->authorization_generation = command.authorization_generation.value_or(account->authorization_generation);
    if (account->revision != command.revision || account->lifecycle != "removing") {
      account->revision = command.revision;
      account->lifecycle = "removing";
      account->operation_id = operation_id;
      account->revision_fingerprint = fingerprint;
      account->operation_phase = "intent";
      new_operation = true;
    } else if (account->operation_phase.empty()) {
      account->operation_phase = "intent";
    }
    if (account->revision_fingerprint.empty())
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
                       "",
                       ""};
    intent_manifest.content_hash = account->revision_fingerprint;
    intent_manifest.operation_phase = account->operation_phase;
    intent_proxy = account->proxy;
  }
  try {
    if (new_operation)
      registry_.write(intent_manifest, intent_proxy);
    {
      std::lock_guard lock(context_.mutex_);
      account->operation_phase = "closing";
      intent_manifest.operation_phase = "closing";
    }
    registry_.write(intent_manifest, intent_proxy);
    close_account(*account, true);
    registry_.remove_account_directory(uuid);
  } catch (...) {
    std::lock_guard lock(context_.mutex_);
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
                            "",
                            ""};
  tombstone.content_hash = account->revision_fingerprint;
  tombstone.operation_phase = "confirmed";
  try {
    registry_.write(tombstone, nlohmann::json{{"mode", "inherit"}, {"http_only", false}});
  } catch (...) {
    std::lock_guard lock(context_.mutex_);
    account->busy = false;
    throw;
  }
  std::lock_guard lock(context_.mutex_);
  account->closed = true;
  account->tombstone = true;
  account->reconciled = false;
  account->busy = false;
  account->lifecycle = "removed";
  account->operation_phase = "confirmed";
  return {{"applied_revision", account->revision}, {"completed", true}};
}

void AccountLifecycleService::close_account(Account &account, bool destroy) {
  std::int32_t client = 0;
  {
    std::unique_lock lock(context_.mutex_);
    account.interests.clear();
    account.interest_counts.clear();
    if (account.closing &&
        !context_.condition_.wait_for(lock, std::chrono::seconds(8), [&account] { return !account.closing; }))
      throw std::runtime_error("operation.outcome_unknown");
    if (account.client_id == 0 || account.closed)
      return;
    client = account.client_id;
  }
  td_api::object_ptr<td_api::Function> function =
      destroy ? td_api::object_ptr<td_api::Function>(td_api::make_object<td_api::destroy>())
              : td_api::object_ptr<td_api::Function>(td_api::make_object<td_api::close>());
  const auto response = broker_.request(client, std::move(function));
  if (td_error_code(response, 504))
    throw std::runtime_error("operation.outcome_unknown");
  if (!response || response->get_id() == td_api::error::ID)
    throw std::runtime_error("telegram.operation_failed");
  std::unique_lock lock(context_.mutex_);
  if (!context_.condition_.wait_for(lock, std::chrono::seconds(8), [&account] { return account.closed; }))
    throw std::runtime_error("operation.outcome_unknown");
  context_.client_accounts_.erase(client);
  account.client_id = 0;
}

} // namespace telebezel::runtime
