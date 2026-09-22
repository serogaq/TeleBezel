#include "fakes/fake_registry.hpp"
#include "fakes/fake_transport.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/td_runtime.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <random>
#include <td/telegram/td_api.h>
#include <thread>

namespace {
namespace td_api = td::td_api;
using telebezel::testing::FakeRegistry;
using telebezel::testing::FakeTransport;

template <class Predicate> void eventually(Predicate predicate, int attempts = 400) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("hardening test wait timed out");
}

std::filesystem::path temporary_root() {
  auto root = std::filesystem::temp_directory_path() / ("telebezel-hardening-" + telebezel::make_request_id());
  std::filesystem::create_directories(root);
  return root;
}

void write_master_key(const std::filesystem::path &path) {
  {
    std::ofstream key(path);
    key << std::string(64, 'a');
  }
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
}

td_api::object_ptr<td_api::chat> make_chat(std::int64_t id) {
  auto chat = td_api::make_object<td_api::chat>();
  chat->id_ = id;
  chat->title_ = "Chat " + std::to_string(id);
  chat->type_ = td_api::make_object<td_api::chatTypePrivate>(id);
  chat->unread_mention_count_ = 2;
  chat->positions_.push_back(
      td_api::make_object<td_api::chatPosition>(td_api::make_object<td_api::chatListMain>(), 100 + id, false, nullptr));
  return chat;
}

td_api::object_ptr<td_api::message> make_message(std::int64_t chat_id, std::int64_t id) {
  auto message = td_api::make_object<td_api::message>();
  message->id_ = id;
  message->chat_id_ = chat_id;
  message->sender_id_ = td_api::make_object<td_api::messageSenderUser>(7);
  return message;
}

td_api::object_ptr<td_api::message> photo_message(std::int64_t id, std::int32_t file_id) {
  auto message = make_message(42, id);
  auto content = td_api::make_object<td_api::messagePhoto>();
  content->photo_ = td_api::make_object<td_api::photo>();
  auto size = td_api::make_object<td_api::photoSize>();
  size->photo_ = td_api::make_object<td_api::file>();
  size->photo_->id_ = file_id;
  size->photo_->size_ = 1024;
  content->photo_->sizes_.push_back(std::move(size));
  message->content_ = std::move(content);
  return message;
}

class HardeningRuntime : public ::testing::Test {
protected:
  std::filesystem::path root = temporary_root();
  telebezel::Config config;
  std::unique_ptr<FakeRegistry> registry;
  FakeTransport *transport = nullptr;
  std::unique_ptr<telebezel::TdRuntime> runtime;
  const std::string uuid = "00112233-4455-4677-8899-aabbccddeeff";
  const std::string generation = "11112233-4455-4677-8899-aabbccddeeff";

  nlohmann::json command(std::uint64_t authorization_generation = 1) const {
    return {{"uuid", uuid},
            {"generation", generation},
            {"revision", 1},
            {"authorization_generation", authorization_generation},
            {"mode", "create"},
            {"operation_id", nullptr},
            {"lifecycle", "provisioning"},
            {"proxy", {{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}}}};
  }
  void SetUp() override {
    config.master_key_file = (root / "master-key").string();
    write_master_key(config.master_key_file);
    config.telegram_api_id = 12345;
    config.telegram_api_hash = "test-api-hash";
    config.internal_token = std::string(32, 'k');
    config.preview_failure_ttl_seconds = 1;
    config.updates_wait_max_seconds = 5;
    registry = std::make_unique<FakeRegistry>(root);
    auto fake = std::make_unique<FakeTransport>();
    transport = fake.get();
    runtime = std::make_unique<telebezel::TdRuntime>(config, *registry, std::move(fake));
    runtime->start();
    ASSERT_TRUE(runtime->reconcile(uuid, command()).value("runtime_available", false));
  }
  void TearDown() override {
    runtime.reset();
    registry.reset();
    std::filesystem::remove_all(root);
  }
  void become_ready() {
    transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
    eventually([&] { return runtime->snapshot(uuid).value("authorization_state", "") == "ready"; });
  }
};

TEST_F(HardeningRuntime, ItemReadCursorKeepsEventsOfOtherChats) {
  become_ready();
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(make_chat(42)));
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(make_chat(43)));
  eventually([&] { return runtime->chats(uuid, "main", 20, "")["items"].size() == 2; });
  transport->precede_response(td_api::getMessageLocally::ID, td_api::make_object<td_api::updateChatTitle>(43, "B"));
  transport->queue_response(td_api::getMessageLocally::ID, make_message(42, 7));
  const auto read = runtime->message(uuid, 42, 7);
  ASSERT_EQ(read["item"].value("id", ""), "7");
  const auto events = runtime->updates(uuid, read.value("updates_cursor", ""), 100)["items"];
  ASSERT_TRUE(std::any_of(events.begin(), events.end(), [](const auto &event) {
    return event.value("chat_id", "") == "43" && event.value("field", "") == "title";
  })) << "an item read advanced the cursor past another chat's event";
}

TEST_F(HardeningRuntime, UnreadCountersAndNotificationSettingsAreProjected) {
  become_ready();
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(make_chat(42)));
  eventually([&] { return runtime->chat(uuid, 42).contains("item"); });
  ASSERT_EQ(runtime->chat(uuid, 42)["item"].value("unread_mention_count", 0), 2);
  transport->emit_update(1, td_api::make_object<td_api::updateChatUnreadMentionCount>(42, 5));
  transport->emit_update(1, td_api::make_object<td_api::updateChatUnreadReactionCount>(42, 3));
  auto settings = td_api::make_object<td_api::chatNotificationSettings>();
  settings->mute_for_ = 3600;
  transport->emit_update(1, td_api::make_object<td_api::updateChatNotificationSettings>(42, std::move(settings)));
  eventually([&] { return runtime->chat(uuid, 42)["item"]["notifications"].value("mute_for", 0) == 3600; });
  const auto item = runtime->chat(uuid, 42)["item"];
  ASSERT_EQ(item.value("unread_mention_count", 0), 5);
  ASSERT_EQ(item.value("unread_reaction_count", 0), 3);
}

TEST_F(HardeningRuntime, UpdatesCanWaitForTheNextEvent) {
  become_ready();
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(make_chat(42)));
  eventually([&] { return runtime->chat(uuid, 42).contains("item"); });
  const auto cursor = runtime->chat(uuid, 42).value("updates_cursor", "");
  const auto started = std::chrono::steady_clock::now();
  auto waiting =
      std::async(std::launch::async, [&] { return runtime->updates(uuid, cursor, 100, std::chrono::seconds(5)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  transport->emit_update(1, td_api::make_object<td_api::updateChatTitle>(42, "Renamed"));
  const auto result = waiting.get();
  ASSERT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(4));
  ASSERT_EQ(result["items"].size(), 1);
  ASSERT_EQ(result["items"][0].value("field", ""), "title");
}

TEST_F(HardeningRuntime, PermanentlyFailedPreviewIsRequeuedAfterItsTtl) {
  become_ready();
  auto history = td_api::make_object<td_api::messages>();
  history->messages_.push_back(photo_message(55, 31));
  transport->queue_response(td_api::getChatHistory::ID, std::move(history));
  for (int failure = 0; failure < 4; ++failure)
    transport->queue_response(td_api::downloadFile::ID, td_api::make_object<td_api::error>(400, "DOWNLOAD_FAILED"));
  const auto state = [&] { return runtime->message(uuid, 42, 55)["item"]["content"].value("preview_state", ""); };
  ASSERT_EQ(runtime->messages(uuid, 42, 1, "")["items"][0]["content"].value("preview_state", ""), "queued");
  for (int failure = 1; failure < 4; ++failure) {
    eventually([&] { return state() == "failed"; });
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_EQ(state(), "queued");
  }
  eventually([&] { return state() == "failed"; });
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  ASSERT_EQ(state(), "failed") << "a permanently failed preview was retried before its ttl";
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  ASSERT_EQ(state(), "queued") << "a permanently failed preview stayed failed after the outage";
}

TEST_F(HardeningRuntime, FloodWaitIsRateLimitedAndPostponesResend) {
  transport->emit_state(
      1, td_api::make_object<td_api::authorizationStateWaitCode>(td_api::make_object<td_api::authenticationCodeInfo>(
             "+15550000000", td_api::make_object<td_api::authenticationCodeTypeSms>(6),
             td_api::make_object<td_api::authenticationCodeTypeCall>(6), 0)));
  eventually([&] { return runtime->snapshot(uuid).value("authorization_state", "") == "awaiting_code"; });
  transport->queue_response(td_api::checkAuthenticationCode::ID,
                            td_api::make_object<td_api::error>(400, "FLOOD_WAIT_120"));
  auto action = command();
  action.erase("mode");
  action.erase("operation_id");
  action.erase("lifecycle");
  action.erase("proxy");
  action["action"] = "submit_code";
  action["value"] = "12345";
  action["authorization_version"] = runtime->snapshot(uuid)["authorization"].value("authorization_version", "");
  const auto result = runtime->authorization_action(uuid, action);
  ASSERT_EQ(result.value("code", ""), "authorization.flood_wait");
  ASSERT_EQ(result.value("status", 0), 429);
  ASSERT_EQ(result.value("retry_after", 0), 120);
  const auto authorization = runtime->snapshot(uuid)["authorization"];
  ASSERT_FALSE(authorization.value("resend_available", true));
  ASSERT_GE(authorization.value("resend_after_seconds", 0), 119);
}

TEST_F(HardeningRuntime, RepeatedAuthorizationStateKeepsItsVersion) {
  const auto wait_code = [] {
    return td_api::make_object<td_api::authorizationStateWaitCode>(td_api::make_object<td_api::authenticationCodeInfo>(
        "+15550000000", td_api::make_object<td_api::authenticationCodeTypeSms>(6), nullptr, 30));
  };
  transport->emit_state(1, wait_code());
  eventually([&] { return runtime->snapshot(uuid).value("authorization_state", "") == "awaiting_code"; });
  const auto version = runtime->snapshot(uuid)["authorization"].value("authorization_version", "");
  transport->emit_state(1, wait_code());
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(make_chat(1)));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_EQ(runtime->snapshot(uuid)["authorization"].value("authorization_version", ""), version);
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateWaitPassword>());
  eventually([&] { return runtime->snapshot(uuid).value("authorization_state", "") == "awaiting_password"; });
  ASSERT_NE(runtime->snapshot(uuid)["authorization"].value("authorization_version", ""), version);
}

class EngineHarness {
public:
  explicit EngineHarness(FakeRegistry &registry, const telebezel::Config &config)
      : store(config), broker(transport), engine(store, registry, transport, broker), codec(config.internal_token) {}
  ~EngineHarness() { engine.stop(); }
  telebezel::runtime::AccountState &activate(const std::string &uuid) {
    engine.start();
    std::lock_guard lock(store.mutex_);
    auto &account = store.accounts_.at(uuid);
    account.client_id = 1;
    account.reconciled = true;
    account.lifecycle = "active";
    store.client_accounts_[1] = uuid;
    return account;
  }
  template <class Body> auto locked(Body body) {
    std::lock_guard lock(store.mutex_);
    return body();
  }
  FakeTransport transport;
  telebezel::runtime::AccountStore store;
  telebezel::runtime::TdRequestBroker broker;
  telebezel::runtime::RuntimeEngine engine;
  telebezel::runtime::CursorCodec codec;
};

class SessionGeneration : public ::testing::Test {
protected:
  std::filesystem::path root = temporary_root();
  telebezel::Config config;
  FakeRegistry registry{root};
  const std::string uuid = "00112233-4455-4677-8899-aabbccddeeff";
  void SetUp() override {
    config.internal_token = std::string(32, 'k');
    telebezel::AccountManifest manifest;
    manifest.uuid = uuid;
    manifest.generation = "11112233-4455-4677-8899-aabbccddeeff";
    manifest.lifecycle = "active";
    registry.write(manifest, nlohmann::json::object());
  }
  void TearDown() override { std::filesystem::remove_all(root); }
};

TEST_F(SessionGeneration, LosingTheSessionDuringAnOperationInvalidatesEveryReadToken) {
  EngineHarness harness(registry, config);
  auto &account = harness.activate(uuid);
  harness.transport.emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  eventually([&] { return harness.locked([&] { return account.authorization_state == "ready"; }); });
  const auto [cursor, fence] = harness.locked([&] {
    account.busy = true;
    return std::pair{harness.codec.encode(account, account.event_sequence), *telebezel::runtime::read_fence(account)};
  });
  harness.transport.emit_state(1, td_api::make_object<td_api::authorizationStateWaitPhoneNumber>());
  eventually([&] { return harness.locked([&] { return account.authorization_state == "awaiting_phone_number"; }); });
  harness.transport.emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  eventually([&] { return harness.locked([&] { return account.authorization_state == "ready"; }); });
  eventually([&] { return registry.read(uuid)->authorization_generation == 2; });
  harness.locked([&] {
    account.busy = false;
    account.reconciled = true;
    EXPECT_EQ(account.authorization_generation, 2U);
    EXPECT_NE(account.runtime_epoch, fence.epoch);
    EXPECT_FALSE(harness.codec.decode(account, cursor).has_value());
    EXPECT_FALSE(telebezel::runtime::fence_valid(account, fence));
    return 0;
  });
}

TEST_F(SessionGeneration, UnpersistedGenerationKeepsReadsClosedAndSurvivesRestart) {
  std::string cursor;
  {
    EngineHarness harness(registry, config);
    auto &account = harness.activate(uuid);
    harness.transport.emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
    eventually([&] { return harness.locked([&] { return account.authorization_state == "ready"; }); });
    cursor = harness.locked([&] { return harness.codec.encode(account, account.event_sequence); });
    registry.set_fail_persist(true);
    harness.transport.emit_state(1, td_api::make_object<td_api::authorizationStateWaitPhoneNumber>());
    harness.transport.emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
    eventually([&] { return harness.locked([&] { return account.last_error == "storage.io_error"; }); });
    harness.locked([&] {
      account.reconciled = true;
      EXPECT_FALSE(telebezel::runtime::read_fence(account).has_value());
      return 0;
    });
    registry.set_fail_persist(false);
    eventually([&] { return registry.read(uuid)->authorization_generation == 2; }, 600);
    eventually([&] {
      return harness.locked([&] {
        account.reconciled = true;
        return telebezel::runtime::read_fence(account).has_value();
      });
    });
  }
  EngineHarness restarted(registry, config);
  auto &account = restarted.activate(uuid);
  restarted.locked([&] {
    EXPECT_EQ(account.authorization_generation, 2U);
    EXPECT_FALSE(restarted.codec.decode(account, cursor).has_value());
    return 0;
  });
}

TEST(ReceiveLoop, SurvivesAnExceptionAndHandlesTheNextUpdate) {
  auto root = temporary_root();
  FakeRegistry registry(root);
  telebezel::Config config;
  telebezel::AccountManifest manifest;
  manifest.uuid = "00112233-4455-4677-8899-aabbccddeeff";
  manifest.generation = "11112233-4455-4677-8899-aabbccddeeff";
  registry.write(manifest, nlohmann::json::object());
  EngineHarness harness(registry, config);
  auto &account = harness.activate(manifest.uuid);
  harness.transport.fail_next_receive();
  harness.transport.emit_update(1, td_api::make_object<td_api::updateNewChat>(make_chat(9)));
  eventually([&] { return harness.locked([&] { return account.chats.contains(9); }); });
  std::filesystem::remove_all(root);
}

TEST(CacheAccounting, InsertsStayCheapAndCountersStayExact) {
  telebezel::Config config;
  config.cache_messages_per_chat = 500;
  config.cache_messages_per_account = 20000;
  config.cache_messages_per_process = 20000;
  telebezel::runtime::AccountStore store(config);
  auto &first = store.accounts_["first"];
  auto &second = store.accounts_["second"];
  const auto started = std::chrono::steady_clock::now();
  for (std::int64_t index = 0; index < 20000; ++index)
    store.put_message(index % 2 == 0 ? first : second, {index % 200, index}, {{"date", index}, {"id", "x"}});
  ASSERT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
  ASSERT_EQ(store.cached_messages(), 20000U);
  ASSERT_TRUE(store.consistent());
  std::mt19937 random(7);
  for (int step = 0; step < 20000; ++step) {
    auto &account = random() % 2 == 0 ? first : second;
    const std::int64_t chat = random() % 50;
    const std::int64_t message = random() % 3000;
    switch (random() % 4) {
    case 0:
      store.erase_message(account, {chat, message});
      break;
    case 1:
      store.put_sender_name(account, "user:" + std::to_string(message), std::string(random() % 20, 'n'));
      break;
    default:
      store.put_message(
          account, {chat, message},
          {{"date", static_cast<std::int64_t>(random() % 100000)}, {"text", std::string(random() % 64, 't')}});
    }
  }
  ASSERT_TRUE(store.consistent());
  store.reset_session(first);
  ASSERT_TRUE(store.consistent());
  ASSERT_EQ(store.cached_messages(), second.messages.size());
}

TEST(WorkerLifecycle, StopJoinsBackgroundThreads) {
  telebezel::Config config;
  FakeTransport transport;
  telebezel::runtime::AccountStore store(config);
  telebezel::runtime::TdRequestBroker broker(transport);
  telebezel::runtime::CursorCodec codec("key");
  telebezel::runtime::ReadModelService reads(store, config, broker, codec);
  telebezel::runtime::InterestLeaseManager leases(store, broker);
  ASSERT_FALSE(reads.running());
  ASSERT_FALSE(leases.running());
  reads.start();
  leases.start();
  ASSERT_TRUE(reads.running());
  ASSERT_TRUE(leases.running());
  leases.stop();
  reads.stop();
  ASSERT_FALSE(reads.running());
  ASSERT_FALSE(leases.running());
  reads.start();
  ASSERT_TRUE(reads.running());
  reads.stop();
}

TEST(InterestLeases, ReleasingUnknownChatsLeavesNoState) {
  telebezel::Config config;
  FakeTransport transport;
  telebezel::runtime::AccountStore store(config);
  telebezel::runtime::TdRequestBroker broker(transport);
  auto &account = store.accounts_["account"];
  account.uuid = "account";
  account.client_id = 1;
  account.reconciled = true;
  account.lifecycle = "active";
  telebezel::runtime::InterestLeaseManager leases(store, broker);
  const std::string lease = "device:80112233-4455-4677-8899-aabbccddeeff:90112233-4455-4677-8899-aabbccddeeff:";
  for (std::int64_t chat = 1; chat <= 5000; ++chat)
    ASSERT_FALSE(leases.set_interest("account", chat, lease + std::to_string(chat), false).value("active", true));
  ASSERT_TRUE(account.interest_states.empty());
  ASSERT_TRUE(account.interest_counts.empty());
}

TEST(RequestBroker, EvictionKeepsPendingClientRecovery) {
  FakeTransport transport;
  telebezel::runtime::TdRequestBroker broker(transport);
  broker.await_request(broker.begin_request(99, td_api::make_object<td_api::getMe>()), std::chrono::milliseconds(0),
                       true);
  for (int index = 0; index < 1100; ++index)
    broker.await_request(broker.begin_request(1, td_api::make_object<td_api::getMe>()), std::chrono::milliseconds(0));
  ASSERT_LE(broker.counts().unresolved, 1024U);
  const auto stalled = broker.expire(std::chrono::steady_clock::now() + std::chrono::minutes(1));
  ASSERT_EQ(stalled, std::vector<std::int32_t>{99});
  ASSERT_EQ(broker.counts().unresolved, 0U);
}
} // namespace
