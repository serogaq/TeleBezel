#include "telebezel/registry.hpp"
#include "telebezel/td_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <td/telegram/td_api.h>
#include <thread>

namespace {
namespace td_api = td::td_api;
void require(bool value) {
  if (!value)
    throw std::runtime_error("runtime test assertion failed");
}

class FakeTransport final : public telebezel::TdTransport {
public:
  std::int32_t create_client_id() override { return next_client_++; }
  void send(std::int32_t client, std::uint64_t request, td_api::object_ptr<td_api::Function> function) override {
    const auto type = function->get_id();
    {
      std::lock_guard lock(mutex_);
      sent_.emplace_back(client, type);
    }
    if (type == td_api::getMe::ID) {
      auto user = td_api::make_object<td_api::user>();
      user->id_ = 9007199254740000LL;
      user->first_name_ = "Ada";
      user->last_name_ = "Lovelace";
      user->phone_number_ = "+15550000000";
      user->is_premium_ = true;
      user->usernames_ = td_api::make_object<td_api::usernames>();
      user->usernames_->active_usernames_ = {"ada"};
      push({client, request, std::move(user)});
    } else if (type == td_api::getProxies::ID) {
      push({client, request, td_api::make_object<td_api::addedProxies>()});
    } else if (type == td_api::addProxy::ID) {
      if (fail_add_proxy_.load())
        push({client, request, td_api::make_object<td_api::error>(400, "PROXY_FAILED")});
      else
        push({client, request, td_api::make_object<td_api::addedProxy>()});
    } else {
      push({client, request, td_api::make_object<td_api::ok>()});
    }
    if (type == td_api::setTdlibParameters::ID) {
      push({client, 0,
            td_api::make_object<td_api::updateAuthorizationState>(
                td_api::make_object<td_api::authorizationStateWaitPhoneNumber>())});
    } else if ((type == td_api::close::ID || type == td_api::destroy::ID) && suppress_close_updates_.load()) {
      // Tests release the storage owner explicitly with authorizationStateClosed.
    } else if (type == td_api::close::ID || type == td_api::destroy::ID || type == td_api::logOut::ID) {
      push({client, 0,
            td_api::make_object<td_api::updateAuthorizationState>(
                td_api::make_object<td_api::authorizationStateClosed>())});
    }
  }
  void emit_state(std::int32_t client, td_api::object_ptr<td_api::AuthorizationState> state) {
    push({client, 0, td_api::make_object<td_api::updateAuthorizationState>(std::move(state))});
  }
  std::vector<std::pair<std::int32_t, std::int32_t>> sent() {
    std::lock_guard lock(mutex_);
    return sent_;
  }
  void set_fail_add_proxy(bool value) { fail_add_proxy_.store(value); }
  void set_suppress_close_updates(bool value) { suppress_close_updates_.store(value); }
  telebezel::TransportResponse receive(double timeout) override {
    std::unique_lock lock(mutex_);
    condition_.wait_for(lock, std::chrono::duration<double>(timeout), [this] { return !responses_.empty(); });
    if (responses_.empty())
      return {};
    auto response = std::move(responses_.front());
    responses_.pop();
    return response;
  }

private:
  void push(telebezel::TransportResponse response) {
    std::lock_guard lock(mutex_);
    responses_.push(std::move(response));
    condition_.notify_one();
  }
  std::int32_t next_client_{1};
  std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<telebezel::TransportResponse> responses_;
  std::vector<std::pair<std::int32_t, std::int32_t>> sent_;
  std::atomic<bool> fail_add_proxy_{false};
  std::atomic<bool> suppress_close_updates_{false};
};

template <class Predicate> void wait_until(Predicate predicate) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("runtime test wait timed out");
}
} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "telebezel-runtime-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto master = root / "master-key";
  {
    std::ofstream output(master);
    output << "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n";
  }
  std::filesystem::permissions(master, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
  telebezel::Config config;
  config.data_directory = (root / "volume").string();
  config.master_key_file = master.string();
  config.telegram_api_id = 12345;
  config.telegram_api_hash = "test-api-hash";
  telebezel::Registry registry(config.data_directory, config.master_key_file);
  registry.open();
  auto fake = std::make_unique<FakeTransport>();
  auto *transport = fake.get();
  telebezel::TdRuntime runtime(config, registry, std::move(fake));
  runtime.start();
  const std::string first = "00112233-4455-4677-8899-aabbccddeeff";
  const std::string second = "10112233-4455-4677-8899-aabbccddeeff";
  const std::string third = "40112233-4455-4677-8899-aabbccddeeff";
  const std::string interrupted_logout = "50112233-4455-4677-8899-aabbccddeeff";
  const std::string interrupted_remove = "60112233-4455-4677-8899-aabbccddeeff";
  const std::string missing_proxy = "70112233-4455-4677-8899-aabbccddeeff";
  const std::string generation = "11112233-4455-4677-8899-aabbccddeeff";
  const auto command = [&generation](const std::string &uuid) {
    return nlohmann::json{
        {"uuid", uuid},
        {"generation", generation},
        {"revision", 1},
        {"mode", "create"},
        {"operation_id", nullptr},
        {"lifecycle", "provisioning"},
        {"proxy", {{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}}}};
  };
  require(runtime.reconcile(first, command(first)).value("runtime_available", false));
  require(runtime.reconcile(second, command(second)).value("runtime_available", false));
  const auto sent = transport->sent();
  require(sent.size() >= 2);
  require(sent[0].second == td_api::setNetworkType::ID);
  require(sent[1].second == td_api::setTdlibParameters::ID);
  auto conflicting = command(first);
  conflicting["proxy"]["id"] = "61112233-4455-4677-8899-aabbccddeeff";
  require(runtime.reconcile(first, conflicting).value("code", "") == "operation.conflict");
  const auto snapshots = runtime.snapshots({first, second});
  require(snapshots["accounts"].size() == 2);
  auto stale_action = command(second);
  stale_action["action"] = "submit_phone_number";
  stale_action["authorization_version"] = "stale";
  stale_action["value"] = "+15550000000";
  require(runtime.authorization_action(second, stale_action).value("code", "") == "authorization.invalid_state");
  const auto assert_state = [&](td_api::object_ptr<td_api::AuthorizationState> state, const std::string &expected) {
    transport->emit_state(2, std::move(state));
    wait_until([&] { return runtime.snapshot(second).value("authorization_state", "") == expected; });
  };
  const auto perform_action = [&](const std::string &action, const std::optional<std::string> &value,
                                  std::int32_t expected_function) {
    auto action_command = command(second);
    action_command["action"] = action;
    action_command["authorization_version"] =
        runtime.snapshot(second)["authorization"].value("authorization_version", "");
    if (value)
      action_command["value"] = *value;
    require(!runtime.authorization_action(second, action_command).value("_error", false));
    require(transport->sent().back().second == expected_function);
  };
  assert_state(td_api::make_object<td_api::authorizationStateWaitTdlibParameters>(), "initializing");
  assert_state(td_api::make_object<td_api::authorizationStateWaitPhoneNumber>(), "awaiting_phone_number");
  perform_action("submit_phone_number", "+15550000000", td_api::setAuthenticationPhoneNumber::ID);
  perform_action("start_qr", std::nullopt, td_api::requestQrCodeAuthentication::ID);
  assert_state(td_api::make_object<td_api::authorizationStateWaitPremiumPurchase>(), "premium_purchase_required");
  assert_state(td_api::make_object<td_api::authorizationStateWaitEmailAddress>(), "awaiting_email_address");
  perform_action("submit_email_address", "a@example.test", td_api::setAuthenticationEmailAddress::ID);
  assert_state(td_api::make_object<td_api::authorizationStateWaitEmailCode>(), "awaiting_email_code");
  perform_action("submit_email_code", "001234", td_api::checkAuthenticationEmailCode::ID);
  perform_action("resend_code", std::nullopt, td_api::resendLoginEmailAddressCode::ID);
  assert_state(td_api::make_object<td_api::authorizationStateWaitRegistration>(), "registration_required");
  assert_state(td_api::make_object<td_api::authorizationStateWaitPassword>(), "awaiting_password");
  perform_action("submit_password", "password-not-logged", td_api::checkAuthenticationPassword::ID);
  assert_state(td_api::make_object<td_api::authorizationStateLoggingOut>(), "logging_out");
  assert_state(td_api::make_object<td_api::authorizationStateClosing>(), "closing");
  assert_state({}, "error");
  require(runtime.snapshot(second).value("last_error_code", "") == "authorization.unsupported_state");
  transport->emit_state(
      2, td_api::make_object<td_api::authorizationStateWaitCode>(td_api::make_object<td_api::authenticationCodeInfo>(
             "+15550000000", td_api::make_object<td_api::authenticationCodeTypeSms>(6),
             td_api::make_object<td_api::authenticationCodeTypeCall>(6), 0)));
  wait_until([&] { return runtime.snapshot(second).value("authorization_state", "") == "awaiting_code"; });
  auto authorization = runtime.snapshot(second)["authorization"];
  require(authorization.value("delivery_method", "") == "sms");
  require(authorization.value("resend_available", false));
  require(std::find(authorization["allowed_actions"].begin(), authorization["allowed_actions"].end(), "resend_code") !=
          authorization["allowed_actions"].end());
  perform_action("submit_code", "001234", td_api::checkAuthenticationCode::ID);
  auto resend = command(second);
  resend["action"] = "resend_code";
  resend["authorization_version"] = authorization["authorization_version"];
  require(!runtime.authorization_action(second, resend).value("_error", false));
  require(transport->sent().back().second == td_api::resendAuthenticationCode::ID);
  transport->emit_state(2, td_api::make_object<td_api::authorizationStateWaitOtherDeviceConfirmation>("tg://login"));
  wait_until(
      [&] { return runtime.snapshot(second)["authorization"].value("state", "") == "awaiting_qr_confirmation"; });
  require(runtime.snapshot(second)["authorization"].value("qr_link", "") == "tg://login");
  transport->emit_state(2,
                        td_api::make_object<td_api::authorizationStateWaitOtherDeviceConfirmation>("tg://refreshed"));
  wait_until([&] { return runtime.snapshot(second)["authorization"].value("qr_link", "") == "tg://refreshed"; });
  transport->emit_state(2, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime.snapshot(second).contains("telegram_identity"); });
  const auto identity = runtime.snapshot(second)["telegram_identity"];
  require(identity.value("id", "") == "9007199254740000");
  require(identity.value("first_name", "") == "Ada");
  require(!identity.contains("phone_number"));
  auto logout = command(second);
  logout["revision"] = 2;
  logout["lifecycle"] = "logout_pending";
  logout["operation_id"] = "31112233-4455-4677-8899-aabbccddeeff";
  logout["logout_operation_id"] = logout["operation_id"];
  require(!runtime.logout(second, logout).value("completed", false));
  wait_until([&] { return runtime.snapshot(second).value("authorization_state", "") == "closed"; });
  require(runtime.logout(second, logout).value("completed", false));
  auto proxy_update = logout;
  proxy_update["revision"] = 3;
  proxy_update["lifecycle"] = "active";
  proxy_update["operation_id"] = "41112233-4455-4677-8899-aabbccddeeff";
  proxy_update.erase("logout_operation_id");
  proxy_update["proxy"] = nlohmann::json{{"id", "71112233-4455-4677-8899-aabbccddeeff"},
                                         {"mode", "http"},
                                         {"host", "proxy.example"},
                                         {"port", 8080},
                                         {"username", "proxy-user"},
                                         {"password", "proxy-password"},
                                         {"http_only", true}};
  require(runtime.update_proxy(second, proxy_update).value("completed", false));
  auto stale_proxy = proxy_update;
  stale_proxy["revision"] = 2;
  bool stale_proxy_rejected = false;
  try {
    static_cast<void>(runtime.update_proxy(second, stale_proxy));
  } catch (const std::runtime_error &error) {
    stale_proxy_rejected = std::string(error.what()) == "operation.conflict";
  }
  require(stale_proxy_rejected);
  auto failing_proxy = command(third);
  failing_proxy["proxy"] = nlohmann::json{{"id", "81112233-4455-4677-8899-aabbccddeeff"},
                                          {"mode", "http"},
                                          {"host", "bad-proxy.example"},
                                          {"port", 8080},
                                          {"http_only", true}};
  transport->set_fail_add_proxy(true);
  transport->set_suppress_close_updates(true);
  const auto sent_before_failure = transport->sent().size();
  auto failed_reconcile = std::async(std::launch::async, [&] { return runtime.reconcile(third, failing_proxy); });
  std::int32_t closing_client = 0;
  wait_until([&] {
    const auto requests = transport->sent();
    const auto close = std::find_if(requests.begin() + static_cast<std::ptrdiff_t>(sent_before_failure), requests.end(),
                                    [](const auto &item) { return item.second == td_api::close::ID; });
    if (close == requests.end())
      return false;
    closing_client = close->first;
    return true;
  });
  require(runtime.reconcile(third, failing_proxy).value("code", "") == "operation.conflict");
  transport->emit_state(closing_client, td_api::make_object<td_api::authorizationStateClosed>());
  require(failed_reconcile.get().value("code", "") == "configuration.invalid");
  const auto failed_activation = transport->sent();
  require(std::count_if(failed_activation.begin() + static_cast<std::ptrdiff_t>(sent_before_failure),
                        failed_activation.end(),
                        [](const auto &item) { return item.second == td_api::setNetworkType::ID; }) == 1);
  transport->set_fail_add_proxy(false);
  transport->set_suppress_close_updates(false);
  require(runtime.reconcile(third, failing_proxy).value("runtime_available", false));
  auto remove = command(first);
  remove["revision"] = 2;
  remove["lifecycle"] = "removing";
  remove["operation_id"] = "21112233-4455-4677-8899-aabbccddeeff";
  require(runtime.remove(first, remove).value("completed", false));
  require(runtime.remove(first, remove).value("completed", false));
  require(registry.read(first)->tombstone);
  bool rejected_tombstone = false;
  try {
    static_cast<void>(runtime.reconcile(first, remove));
  } catch (const std::runtime_error &error) {
    rejected_tombstone = std::string(error.what()) == "account.gone";
  }
  require(rejected_tombstone);
  runtime.stop();

  registry.ensure_account_directories(interrupted_logout);
  telebezel::AccountManifest logout_manifest{1,
                                             interrupted_logout,
                                             generation,
                                             false,
                                             1,
                                             1,
                                             "logout_pending",
                                             "81112233-4455-4677-8899-aabbccddeeff",
                                             false,
                                             nullptr,
                                             "",
                                             "intent"};
  registry.write(
      logout_manifest,
      nlohmann::json{{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}});
  registry.ensure_account_directories(interrupted_remove);
  telebezel::AccountManifest remove_manifest{1,          interrupted_remove,
                                             generation, false,
                                             1,          1,
                                             "removing", "91112233-4455-4677-8899-aabbccddeeff",
                                             false,      nullptr,
                                             "",         "closing"};
  registry.write(
      remove_manifest,
      nlohmann::json{{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}});

  runtime.start();
  auto resumed_logout = command(interrupted_logout);
  resumed_logout["lifecycle"] = "logout_pending";
  resumed_logout["operation_id"] = logout_manifest.operation_id;
  resumed_logout["logout_operation_id"] = logout_manifest.operation_id;
  const auto before_resumed_logout = transport->sent();
  const auto logout_requests_before = std::count_if(before_resumed_logout.begin(), before_resumed_logout.end(),
                                                    [](const auto &item) { return item.second == td_api::logOut::ID; });
  require(!runtime.logout(interrupted_logout, resumed_logout).value("completed", false));
  wait_until([&] { return runtime.snapshot(interrupted_logout).value("authorization_state", "") == "closed"; });
  const auto after_resumed_logout = transport->sent();
  require(std::count_if(after_resumed_logout.begin(), after_resumed_logout.end(), [](const auto &item) {
            return item.second == td_api::logOut::ID;
          }) == logout_requests_before + 1);
  require(runtime.logout(interrupted_logout, resumed_logout).value("completed", false));

  auto resumed_remove = command(interrupted_remove);
  resumed_remove["lifecycle"] = "removing";
  resumed_remove["operation_id"] = remove_manifest.operation_id;
  require(runtime.remove(interrupted_remove, resumed_remove).value("completed", false));
  require(registry.read(interrupted_remove)->tombstone);

  auto id_only = command(missing_proxy);
  id_only["proxy"] = nlohmann::json{{"id", "a1112233-4455-4677-8899-aabbccddeeff"}};
  require(runtime.reconcile(missing_proxy, id_only).value("code", "") == "configuration.missing");
  id_only["proxy"] =
      nlohmann::json{{"id", "a1112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}};
  require(runtime.reconcile(missing_proxy, id_only).value("runtime_available", false));
  runtime.stop();
  std::filesystem::remove_all(root);
  return 0;
}
