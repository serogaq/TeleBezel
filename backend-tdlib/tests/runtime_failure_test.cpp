#include "fakes/fake_registry.hpp"
#include "fakes/fake_transport.hpp"
#include "mocks/mock_transport.hpp"
#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include "telebezel/td_runtime.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <latch>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() / ("telebezel-failures-" + telebezel::make_request_id());
  TemporaryDirectory() { std::filesystem::create_directories(path); }
  ~TemporaryDirectory() { std::filesystem::remove_all(path); }
};
TEST(RuntimeFailure, SendFailureCleanup) {
  ::testing::StrictMock<telebezel::testing::MockTransport> transport;
  telebezel::runtime::TdRequestBroker broker(transport);
  EXPECT_CALL(transport, send(1, ::testing::_, ::testing::_))
      .Times(1025)
      .WillRepeatedly(::testing::Throw(std::runtime_error("service.tdlib_unavailable")));
  for (int attempt = 0; attempt < 1025; ++attempt) {
    EXPECT_THROW(broker.begin_request(1, td::td_api::make_object<td::td_api::getMe>()), std::runtime_error);
    ASSERT_EQ(broker.counts().pending, 0U);
  }
  ::testing::Mock::VerifyAndClearExpectations(&transport);
  EXPECT_CALL(transport, send(1, ::testing::_, ::testing::_)).Times(1);
  auto pending = broker.begin_request(1, td::td_api::make_object<td::td_api::getMe>());
  ASSERT_TRUE((pending.pending)) << "send failures exhausted request capacity";
  auto timeout = broker.await_request(std::move(pending), std::chrono::seconds(0), false);
  ASSERT_TRUE((timeout->get_id() == td::td_api::error::ID)) << "timeout must return an error";
  ASSERT_TRUE((broker.counts().pending == 0 && broker.counts().unresolved == 1)) << "timeout lost recovery bookkeeping";
  telebezel::runtime::AccountState account;
  account.client_id = 1;
  ::testing::Mock::VerifyAndClearExpectations(&transport);
  EXPECT_CALL(transport, send(1, ::testing::_, ::testing::_))
      .WillOnce(::testing::Throw(std::runtime_error("service.tdlib_unavailable")));
  const auto identity_error = broker.request_identity(account.client_id);
  ASSERT_TRUE((broker.counts().asynchronous == 0)) << "identity send failure leaked asynchronous request";
  ASSERT_TRUE((identity_error == "service.tdlib_unavailable")) << "identity failure was not projected";
  broker.stop();
  auto stopped = broker.begin_request(1, td::td_api::make_object<td::td_api::getMe>());
  ASSERT_TRUE((!stopped.pending)) << "shutdown must reject new sends";
}
TEST(RuntimeFailure, CorrelationCapacityAndShutdown) {
  telebezel::testing::FakeTransport transport;
  telebezel::runtime::TdRequestBroker broker(transport);
  auto request = broker.begin_request(7, td::td_api::make_object<td::td_api::getMe>());
  telebezel::TransportResponse wrong{8, request.request_id, td::td_api::make_object<td::td_api::ok>()};
  ASSERT_TRUE((!broker.complete(wrong))) << "wrong-client response became identity response";
  ASSERT_TRUE((request.future.wait_for(std::chrono::seconds(0)) == std::future_status::timeout))
      << "wrong-client response completed another client request";
  telebezel::TransportResponse correct{7, request.request_id, td::td_api::make_object<td::td_api::ok>()};
  broker.complete(correct);
  ASSERT_TRUE((broker.await_request(std::move(request), std::chrono::seconds(0))->get_id() == td::td_api::ok::ID))
      << "matching response was lost";
  auto timed = broker.begin_request(7, td::td_api::make_object<td::td_api::getMe>());
  const auto late_id = timed.request_id;
  broker.await_request(std::move(timed), std::chrono::seconds(0), true);
  ASSERT_TRUE(
      (broker.expire(std::chrono::steady_clock::now() + std::chrono::seconds(30)) == std::vector<std::int32_t>{7}))
      << "timed-out client was not scheduled for recovery";
  ASSERT_TRUE((broker.expire(std::chrono::steady_clock::now() + std::chrono::seconds(31)).empty()))
      << "recovery repeated without a new timeout";
  telebezel::TransportResponse late{7, late_id, td::td_api::make_object<td::td_api::ok>()};
  broker.complete(late);
  ASSERT_TRUE((broker.counts().unresolved == 0)) << "late reply retained unresolved state";
  auto pending = broker.begin_request(7, td::td_api::make_object<td::td_api::getMe>());
  for (int i = 1; i < 1024; ++i)
    static_cast<void>(broker.begin_request(7, td::td_api::make_object<td::td_api::getMe>()));
  auto saturated = broker.request(7, td::td_api::make_object<td::td_api::getMe>(), std::chrono::seconds(0));
  ASSERT_TRUE((saturated->get_id() == td::td_api::error::ID &&
               static_cast<td::td_api::error &>(*saturated).message_ == "service.busy"))
      << "request capacity was not bounded";
  broker.stop();
  auto stopped = broker.await_request(std::move(pending), std::chrono::seconds(0));
  ASSERT_TRUE((stopped->get_id() == td::td_api::error::ID &&
               static_cast<td::td_api::error &>(*stopped).message_ == "service.stopping"))
      << "shutdown failed to complete a waiting request";
  ASSERT_TRUE((broker.counts().pending == 0)) << "shutdown retained pending requests";
}
TEST(RuntimeFailure, TypedCommandValidation) {
  const std::string uuid = "00112233-4455-4677-8899-aabbccddeeff";
  nlohmann::json valid{{"uuid", uuid},
                       {"generation", "11112233-4455-4677-8899-aabbccddeeff"},
                       {"revision", 1},
                       {"action", "submit_code"},
                       {"authorization_version", "2"},
                       {"value", "001234"}};
  const auto command = telebezel::runtime::AuthorizationCommand::parse(uuid, valid);
  ASSERT_TRUE((command.action == telebezel::runtime::AuthorizationAction::code && command.value == "001234"))
      << "typed authorization lost leading zeroes";
  for (const auto &patch : std::vector<nlohmann::json>{{{"revision", 0}},
                                                       {{"revision", "1"}},
                                                       {{"authorization_generation", -1}},
                                                       {{"generation", "invalid"}},
                                                       {{"value", 1234}},
                                                       {{"action", "arbitraryCommand"}},
                                                       {{"telegram_api_id", 2147483648LL}}}) {
    auto invalid = valid;
    invalid.update(patch);
    bool rejected = false;
    try {
      static_cast<void>(telebezel::runtime::AuthorizationCommand::parse(uuid, invalid));
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()) == "request.invalid";
    }
    ASSERT_TRUE((rejected)) << "invalid command passed the typed boundary";
  }
}
TEST(RuntimeFailure, CursorBoundariesAndJournalRetention) {
  telebezel::runtime::AccountState account;
  account.uuid = "00112233-4455-4677-8899-aabbccddeeff";
  account.generation = "11112233-4455-4677-8899-aabbccddeeff";
  account.runtime_epoch = "epoch";
  telebezel::runtime::CursorCodec codec("test-key");
  const auto cursor = codec.encode(account, 17);
  ASSERT_TRUE((codec.decode(account, cursor) == 17)) << "valid cursor rejected";
  auto changed = account;
  changed.uuid = "another-account";
  ASSERT_TRUE((!codec.decode(changed, cursor))) << "cursor crossed account boundary";
  changed = account;
  changed.generation = "another-storage-generation";
  ASSERT_TRUE((!codec.decode(changed, cursor))) << "cursor crossed storage generation";
  changed = account;
  ++changed.authorization_generation;
  ASSERT_TRUE((!codec.decode(changed, cursor))) << "cursor survived logout generation";
  changed = account;
  changed.runtime_epoch = "restarted";
  ASSERT_TRUE((!codec.decode(changed, cursor))) << "cursor survived runtime restart";
  ASSERT_TRUE((!codec.decode(account, cursor + "0"))) << "tampered cursor accepted";
  for (int event = 0; event < 5001; ++event)
    telebezel::runtime::UpdateJournal::append(account, "message", 9007199254740993LL, 9007199254740995LL);
  ASSERT_TRUE((account.events.size() == 5000)) << "update journal is unbounded";
  ASSERT_TRUE((account.events.front().at("sequence") == 2)) << "journal removed wrong end";
  ASSERT_TRUE((account.events.back().at("chat_id") == "9007199254740993")) << "chat id lost precision";
  ASSERT_TRUE((account.events.back().at("message_id") == "9007199254740995")) << "message id lost precision";
}
TEST(RuntimeFailure, DurableCompletion) {
  TemporaryDirectory temporary;
  telebezel::Config config;
  config.data_directory = temporary.path.string();
  config.master_key_file = (temporary.path / "master-key").string();
  config.telegram_api_id = 12345;
  config.telegram_api_hash = "test-api-hash";
  {
    std::ofstream key(config.master_key_file);
    key << "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n";
  }
  std::filesystem::permissions(config.master_key_file,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  telebezel::testing::FakeRegistry registry(temporary.path);
  auto fake = std::make_unique<telebezel::testing::FakeTransport>();
  auto *transport = fake.get();
  telebezel::TdRuntime runtime(config, registry, std::move(fake));
  runtime.start();
  const std::string uuid = "00112233-4455-4677-8899-aabbccddeeff";
  nlohmann::json command = {{"uuid", uuid},
                            {"generation", "11112233-4455-4677-8899-aabbccddeeff"},
                            {"revision", 1},
                            {"mode", "create"},
                            {"lifecycle", "provisioning"},
                            {"operation_id", nullptr},
                            {"proxy", {{"mode", "direct"}, {"id", "21112233-4455-4677-8899-aabbccddeeff"}}}};
  registry.fail_write_after(1);
  ASSERT_TRUE((runtime.reconcile(uuid, command).value("code", "") == "storage.write_failed"))
      << "intent write failure was hidden";
  ASSERT_TRUE((transport->sent().empty())) << "TDLib activated before durable intent";
  ASSERT_TRUE((!registry.read(uuid).has_value())) << "failed intent write mutated registry";
  registry.fail_write_after(2);
  ASSERT_TRUE((runtime.reconcile(uuid, command).value("code", "") == "storage.write_failed"))
      << "completion write failure was hidden";
  ASSERT_TRUE((registry.read(uuid)->lifecycle == "provisioning")) << "failed completion advanced durable state";
  ASSERT_TRUE((runtime.reconcile(uuid, command).value("runtime_available", false)))
      << "retry failed to recover completion";
  ASSERT_TRUE((registry.read(uuid)->lifecycle == "active")) << "retry falsely acknowledged an unpersisted completion";
  runtime.stop();
  const auto sends = transport->sent().size();
  runtime.start();
  ASSERT_TRUE((transport->sent().size() == sends)) << "discovered sessions activated without reconciliation";
  runtime.stop();
}
TEST(RuntimeFailure, ProjectionUpdatesAndLeaseExpiry) {
  namespace api = td::td_api;
  TemporaryDirectory directory;
  telebezel::testing::FakeRegistry registry(directory.path);
  telebezel::testing::FakeTransport transport;
  telebezel::Config config;
  telebezel::runtime::AccountStore store(config);
  telebezel::runtime::TdRequestBroker broker(transport);
  telebezel::runtime::RuntimeEngine engine(store, registry, transport, broker);
  auto &account = store.accounts_["account"];
  account.uuid = "account";
  account.client_id = 1;
  account.reconciled = true;
  account.lifecycle = "active";
  store.client_accounts_[1] = "account";
  const auto apply = [&](api::object_ptr<api::Object> update) { engine.handle_update(1, *update); };
  auto chat = api::make_object<api::chat>();
  chat->id_ = 42;
  chat->type_ = api::make_object<api::chatTypeSupergroup>(7, true);
  chat->title_ = "Channel";
  apply(api::make_object<api::updateNewChat>(std::move(chat)));
  ASSERT_TRUE((account.chats[42]["type"] == "channel")) << "channel projection lost type";
  apply(api::make_object<api::updateChatPosition>(
      42,
      api::make_object<api::chatPosition>(api::make_object<api::chatListMain>(), 9007199254740993LL, true, nullptr)));
  ASSERT_TRUE((account.chats[42]["positions"]["main"]["order"] == "9007199254740993")) << "order lost int64 precision";
  auto last = api::make_object<api::message>();
  last->id_ = 99;
  last->chat_id_ = 42;
  apply(api::make_object<api::updateChatLastMessage>(42, std::move(last),
                                                     std::vector<api::object_ptr<api::chatPosition>>{}));
  auto content = api::make_object<api::messageText>();
  content->text_ = api::make_object<api::formattedText>();
  content->text_->text_ = "Edited";
  apply(api::make_object<api::updateMessageContent>(42, 99, std::move(content)));
  apply(api::make_object<api::updateMessageEdited>(42, 99, 123, nullptr));
  ASSERT_TRUE((account.messages[{42, 99}]["content"]["text"] == "Edited")) << "message content stayed stale";
  ASSERT_TRUE((account.chats[42]["last_message"]["edit_date"] == 123)) << "last message edit stayed stale";
  apply(api::make_object<api::updateChatReadInbox>(42, 99, 2));
  apply(api::make_object<api::updateChatReadOutbox>(42, 98));
  apply(api::make_object<api::updateChatIsMarkedAsUnread>(42, true));
  ASSERT_TRUE((account.chats[42]["last_read_inbox_message_id"] == "99" && account.chats[42]["unread_count"] == 2 &&
               account.chats[42]["is_marked_unread"] == true))
      << "read markers lost";
  ASSERT_TRUE((account.chats[42]["last_read_outbox_message_id"] == "98")) << "outbox marker lost";
  apply(api::make_object<api::updateDeleteMessages>(42, std::vector<std::int64_t>{99}, true, false));
  ASSERT_TRUE((!account.messages.contains({42, 99}) && account.chats[42]["last_message"].is_null()))
      << "deleted message remained readable";
  ASSERT_TRUE((account.events.size() >= 8)) << "projection changes did not emit invalidations";
  const auto sequence = account.event_sequence;
  auto unrelated = api::make_object<api::updateChatTitle>(42, "Other account");
  engine.handle_update(999, *unrelated);
  ASSERT_TRUE((account.event_sequence == sequence && account.chats[42]["title"] == "Channel"))
      << "unmapped client contaminated account";

  const auto now = std::chrono::steady_clock::now();
  account.interests["expired"] = {42, now - std::chrono::seconds(1)};
  account.interest_counts[42] = 1;
  account.interests["expired-shared"] = {43, now - std::chrono::seconds(1)};
  account.interests["live-shared"] = {43, now + std::chrono::hours(1)};
  account.interest_counts[43] = 2;
  telebezel::runtime::InterestLeaseManager leases(store, broker);
  engine.start();
  leases.start();
  bool expired = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    {
      std::lock_guard lock(store.mutex_);
      expired = account.interests.size() == 1;
    }
    const auto current_sent = transport.sent();
    if (expired && std::count_if(current_sent.begin(), current_sent.end(),
                                 [](const auto &entry) { return entry.second == api::closeChat::ID; }) == 1)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  leases.stop();
  engine.stop();
  const auto sent = transport.sent();
  const auto closes =
      std::count_if(sent.begin(), sent.end(), [](const auto &entry) { return entry.second == api::closeChat::ID; });
  ASSERT_TRUE((expired && closes == 1)) << "expiry did not close exactly the unreferenced chat";
  ASSERT_TRUE((telebezel::valid_uuid("019966a1-2345-7123-8123-123456789abc"))) << "Laravel UUIDv7 principal rejected";
}
TEST(RequestBrokerConcurrency, ParallelSendReceiveAndShutdown) {
  telebezel::testing::FakeTransport transport;
  telebezel::runtime::TdRequestBroker broker(transport);
  std::atomic<bool> finished{false};
  std::atomic<int> failures{0};
  std::thread receiver([&] {
    while (!finished.load()) {
      auto response = transport.receive(0.01);
      if (response.object)
        broker.complete(response);
    }
  });
  std::vector<std::thread> callers;
  for (int client = 1; client <= 8; ++client) {
    callers.emplace_back([&, client] {
      for (int request = 0; request < 64; ++request) {
        const auto result =
            broker.request(client, td::td_api::make_object<td::td_api::getMe>(), std::chrono::seconds(2));
        if (!result || result->get_id() != td::td_api::user::ID)
          ++failures;
      }
    });
  }
  for (auto &caller : callers)
    caller.join();
  finished.store(true);
  receiver.join();
  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(broker.counts().pending, 0U);
  EXPECT_EQ(transport.sent().size(), 512U);
  callers.clear();
  std::latch ready(8);
  for (int client = 1; client <= 8; ++client) {
    callers.emplace_back([&, client] {
      auto pending = broker.begin_request(client, td::td_api::make_object<td::td_api::getMe>());
      ready.count_down();
      const auto result = broker.await_request(std::move(pending), std::chrono::seconds(2));
      if (!result || result->get_id() != td::td_api::error::ID ||
          static_cast<const td::td_api::error &>(*result).message_ != "service.stopping")
        ++failures;
    });
  }
  ready.wait();
  broker.stop();
  for (auto &caller : callers)
    caller.join();
  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(broker.counts().pending, 0U);
}
} // namespace
