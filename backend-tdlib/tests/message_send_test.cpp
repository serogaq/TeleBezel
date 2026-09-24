#include "fakes/fake_transport.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/td_runtime.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <td/telegram/td_api.h>
#include <thread>

namespace {
namespace td_api = td::td_api;
using telebezel::testing::FakeTransport;

template <class Predicate> void eventually(Predicate predicate, int attempts = 400) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("send test wait timed out");
}

td_api::object_ptr<td_api::chat> make_chat(std::int64_t id, td_api::object_ptr<td_api::ChatType> type,
                                           bool basic_allowed = true) {
  auto chat = td_api::make_object<td_api::chat>();
  chat->id_ = id;
  chat->title_ = "Chat " + std::to_string(id);
  chat->type_ = std::move(type);
  chat->permissions_ = td_api::make_object<td_api::chatPermissions>();
  chat->permissions_->can_send_basic_messages_ = basic_allowed;
  chat->positions_.push_back(
      td_api::make_object<td_api::chatPosition>(td_api::make_object<td_api::chatListMain>(), 100 + id, false, nullptr));
  return chat;
}

td_api::object_ptr<td_api::message> text_message(std::int64_t chat, std::int64_t id, const std::string &text,
                                                 std::int64_t sender = 7) {
  auto message = td_api::make_object<td_api::message>();
  message->id_ = id;
  message->chat_id_ = chat;
  message->date_ = 1700000000;
  message->sender_id_ = td_api::make_object<td_api::messageSenderUser>(sender);
  auto content = td_api::make_object<td_api::messageText>();
  content->text_ = td_api::make_object<td_api::formattedText>();
  content->text_->text_ = text;
  message->content_ = std::move(content);
  return message;
}

std::vector<nlohmann::json> events(telebezel::TdRuntime &runtime, const std::string &uuid, const std::string &cursor,
                                   const std::string &type) {
  std::vector<nlohmann::json> result;
  const auto page = runtime.updates(uuid, cursor, 100);
  for (const auto &event : page["items"])
    if (event.value("type", "") == type)
      result.push_back(event);
  return result;
}
} // namespace

class MessageSendTest : public ::testing::Test {
protected:
  std::filesystem::path root;
  telebezel::Config config;
  std::unique_ptr<telebezel::Registry> registry;
  FakeTransport *transport = nullptr;
  std::unique_ptr<telebezel::TdRuntime> runtime;
  const std::string account = "00112233-4455-4677-8899-aabbccddeeff";
  const std::string generation = "11112233-4455-4677-8899-aabbccddeeff";

  nlohmann::json command() const {
    return {{"uuid", account},
            {"generation", generation},
            {"revision", 1},
            {"mode", "create"},
            {"operation_id", nullptr},
            {"lifecycle", "provisioning"},
            {"proxy", {{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}}}};
  }
  nlohmann::json send(const std::string &operation, const std::string &text, const std::string &reply = {},
                      std::int64_t chat = 42) {
    nlohmann::json body{{"operation_id", operation},
                        {"storage_generation", generation},
                        {"authorization_generation", runtime->snapshot(account).value("authorization_generation", 1)},
                        {"text", text}};
    if (!reply.empty())
      body["reply_to_message_id"] = reply;
    return runtime->send_message(account, chat, body);
  }
  nlohmann::json status(const std::string &operation) {
    return runtime->send_status(account, {operation})["operations"][operation];
  }
  void SetUp() override {
    root = std::filesystem::temp_directory_path() / ("telebezel-send-" + telebezel::make_request_id());
    std::filesystem::create_directories(root);
    config.data_directory = (root / "volume").string();
    config.master_key_file = (root / "master-key").string();
    {
      std::ofstream key(config.master_key_file);
      key << std::string(64, 'a');
    }
    std::filesystem::permissions(config.master_key_file,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
    config.telegram_api_id = 12345;
    config.telegram_api_hash = "test-api-hash";
    registry = std::make_unique<telebezel::Registry>(config.data_directory, config.master_key_file);
    registry->open();
    auto fake = std::make_unique<FakeTransport>();
    transport = fake.get();
    transport->set_answer_lookups(true);
    runtime = std::make_unique<telebezel::TdRuntime>(config, *registry, std::move(fake));
    runtime->start();
    ASSERT_TRUE(runtime->reconcile(account, command()).value("runtime_available", false));
    transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
    eventually([&] { return runtime->snapshot(account).value("authorization_state", "") == "ready"; });
    transport->emit_update(
        1, td_api::make_object<td_api::updateNewChat>(make_chat(42, td_api::make_object<td_api::chatTypePrivate>(7))));
    eventually([&] { return runtime->chat(account, 42).contains("item"); });
  }
  void TearDown() override {
    runtime.reset();
    registry.reset();
    std::filesystem::remove_all(root);
  }
};

TEST_F(MessageSendTest, AcknowledgedSendIsReportedOnceAndReplacesTheTemporaryMessage) {
  const auto cursor = runtime->updates(account, "", 100).value("cursor", "");
  transport->set_auto_complete(true);
  const auto operation = "a0112233-4455-4677-8899-aabbccddeeff";
  const auto result = send(operation, "Hello from the wrist");
  ASSERT_EQ(result.value("state", ""), "sent");
  ASSERT_EQ(result.value("message_id", ""), "5000");
  ASSERT_FALSE(result.value("reply_dropped", true));
  ASSERT_EQ(transport->sends().size(), 1U);
  ASSERT_EQ(transport->sends()[0].text, "Hello from the wrist");
  ASSERT_NE(transport->sends()[0].sending_id, 0);
  const auto repeated = send(operation, "Hello from the wrist");
  ASSERT_EQ(repeated.value("state", ""), "sent");
  ASSERT_EQ(transport->sends().size(), 1U);
  const auto changes = events(*runtime, account, cursor, "send_changed");
  ASSERT_EQ(changes.size(), 2U);
  ASSERT_EQ(changes[0].value("state", ""), "pending");
  ASSERT_EQ(changes[1].value("state", ""), "sent");
  ASSERT_EQ(changes[1].value("operation_id", ""), operation);
  ASSERT_FALSE(changes[1].contains("temporary_message_id"));
  const auto deleted = events(*runtime, account, cursor, "message_deleted");
  ASSERT_EQ(deleted.size(), 1U);
  ASSERT_EQ(deleted[0].value("message_id", ""), "1000000");
  const auto final_message = runtime->message(account, 42, 5000);
  ASSERT_EQ(final_message["item"].value("sending_state", nlohmann::json(nullptr)), nullptr);
  ASSERT_TRUE(final_message["item"]["content"].value("text", "") == "Hello from the wrist");
}

TEST_F(MessageSendTest, PendingSendResolvesLateWithoutBeingSentAgain) {
  const auto operation = "b0112233-4455-4677-8899-aabbccddeeff";
  const auto started = std::chrono::steady_clock::now();
  const auto pending = send(operation, "Later");
  ASSERT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(8));
  ASSERT_EQ(pending.value("state", ""), "pending");
  ASSERT_EQ(pending.value("temporary_message_id", ""), "1000000");
  ASSERT_EQ(runtime->message(account, 42, 1000000)["item"].value("sending_state", ""), "pending");
  ASSERT_EQ(status(operation).value("state", ""), "pending");
  auto repeated = std::async(std::launch::async, [&] { return send(operation, "Later"); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_TRUE(transport->complete_send(1000000, 777, false));
  ASSERT_EQ(repeated.get().value("state", ""), "sent");
  ASSERT_EQ(transport->sends().size(), 1U);
  ASSERT_EQ(status(operation).value("message_id", ""), "777");
}

TEST_F(MessageSendTest, ReplyThatCannotBeRepliedToIsRefusedWithoutSending) {
  transport->set_deny_replies(true);
  const auto result = send("c0112233-4455-4677-8899-aabbccddeeff", "Reply", "55");
  ASSERT_EQ(result.value("state", ""), "failed");
  ASSERT_EQ(result["error"].value("code", ""), "message.reply_unavailable");
  ASSERT_FALSE(result.value("retryable", true));
  ASSERT_TRUE(transport->sends().empty());
  transport->set_deny_replies(false);
  transport->queue_response(td_api::getMessage::ID, td_api::make_object<td_api::error>(404, "Message not found"));
  const auto missing = send("c1112233-4455-4677-8899-aabbccddeeff", "Reply", "56");
  ASSERT_EQ(missing["error"].value("code", ""), "message.reply_unavailable");
  ASSERT_TRUE(transport->sends().empty());
}

TEST_F(MessageSendTest, ReplyKeepsItsTargetOrReportsThatTelegramDroppedIt) {
  const auto before = runtime->updates(account, "", 100).value("cursor", "");
  transport->emit_update(1, td_api::make_object<td_api::updateNewMessage>(text_message(42, 55, "Original question")));
  eventually([&] { return !events(*runtime, account, before, "message_changed").empty(); });
  transport->set_auto_complete(true);
  const auto kept = send("d0112233-4455-4677-8899-aabbccddeeff", "Answer", "55");
  ASSERT_EQ(kept.value("state", ""), "sent");
  ASSERT_FALSE(kept.value("reply_dropped", true));
  ASSERT_EQ(transport->sends()[0].reply_to, 55);
  const auto projected = runtime->message(account, 42, 5000)["item"];
  ASSERT_EQ(projected["reply_to"].value("message_id", ""), "55");
  ASSERT_EQ(projected["reply_to"].value("text", ""), "Original question");
  ASSERT_EQ(projected["reply_to"].value("sender_name", ""), "User 7");
  transport->set_auto_complete(false);
  const auto dropped = send("d1112233-4455-4677-8899-aabbccddeeff", "Answer again", "55");
  ASSERT_EQ(dropped.value("state", ""), "pending");
  ASSERT_TRUE(transport->complete_send(transport->sends()[1].temporary_id, 5100, true));
  eventually([&] { return status("d1112233-4455-4677-8899-aabbccddeeff").value("state", "") == "sent"; });
  ASSERT_TRUE(status("d1112233-4455-4677-8899-aabbccddeeff").value("reply_dropped", false));
}

TEST_F(MessageSendTest, TelegramRefusalsAreTranslatedIntoSafeCodes) {
  transport->queue_response(td_api::sendMessage::ID,
                            td_api::make_object<td_api::error>(403, "CHAT_WRITE_FORBIDDEN secret detail"));
  const auto forbidden = send("e0112233-4455-4677-8899-aabbccddeeff", "No rights");
  ASSERT_EQ(forbidden.value("state", ""), "failed");
  ASSERT_EQ(forbidden["error"].value("code", ""), "message.send_forbidden");
  ASSERT_FALSE(forbidden.value("retryable", true));
  ASSERT_EQ(forbidden.dump().find("secret"), std::string::npos);
  transport->queue_response(td_api::sendMessage::ID, td_api::make_object<td_api::error>(400, "MESSAGE_TOO_LONG"));
  ASSERT_EQ(send("e1112233-4455-4677-8899-aabbccddeeff", "Long")["error"].value("code", ""), "message.text_too_long");
  const auto flood = "e2112233-4455-4677-8899-aabbccddeeff";
  ASSERT_EQ(send(flood, "Fast").value("state", ""), "pending");
  const auto temporary = transport->sends().back().temporary_id;
  ASSERT_TRUE(transport->reject_send(temporary, temporary + 1, 429, "FLOOD_WAIT_30", 30, true));
  eventually([&] { return status(flood).value("state", "") == "failed"; });
  ASSERT_EQ(status(flood)["error"].value("code", ""), "message.send_rate_limited");
  ASSERT_EQ(status(flood)["error"].value("retry_after", 0), 30);
  ASSERT_TRUE(status(flood).value("retryable", false));
  ASSERT_EQ(runtime->message(account, 42, temporary + 1)["item"].value("sending_state", ""), "failed");
  ASSERT_FALSE(runtime->message(account, 42, temporary).contains("item"));
}

TEST_F(MessageSendTest, FenceAndChatAreCheckedBeforeAnythingIsSent) {
  nlohmann::json stale{{"operation_id", "f0112233-4455-4677-8899-aabbccddeeff"},
                       {"storage_generation", generation},
                       {"authorization_generation", 99},
                       {"text", "stale"}};
  ASSERT_THROW(runtime->send_message(account, 42, stale), std::runtime_error);
  stale["authorization_generation"] = 1;
  stale["storage_generation"] = "21112233-4455-4677-8899-aabbccddeeff";
  ASSERT_THROW(runtime->send_message(account, 42, stale), std::runtime_error);
  try {
    send("f1112233-4455-4677-8899-aabbccddeeff", "Nowhere", {}, 404);
    FAIL() << "unknown chat was accepted";
  } catch (const std::runtime_error &error) {
    ASSERT_STREQ(error.what(), "chat.not_found");
  }
  nlohmann::json empty{{"operation_id", "f2112233-4455-4677-8899-aabbccddeeff"},
                       {"storage_generation", generation},
                       {"authorization_generation", 1},
                       {"text", ""}};
  ASSERT_THROW(runtime->send_message(account, 42, empty), std::runtime_error);
  ASSERT_TRUE(transport->sends().empty());
  const auto operation = "f3112233-4455-4677-8899-aabbccddeeff";
  transport->set_auto_complete(true);
  send(operation, "Here");
  nlohmann::json moved{{"operation_id", operation},
                       {"storage_generation", generation},
                       {"authorization_generation", 1},
                       {"text", "Here"},
                       {"reply_to_message_id", "8"}};
  try {
    runtime->send_message(account, 42, moved);
    FAIL() << "a reused operation with a different target was accepted";
  } catch (const std::runtime_error &error) {
    ASSERT_STREQ(error.what(), "operation.conflict");
  }
}

TEST_F(MessageSendTest, UnansweredSendMessageKeepsTheOperationAndStillResolves) {
  transport->queue_response(td_api::sendMessage::ID, nullptr);
  const auto operation = "a1112233-4455-4677-8899-aabbccddeeff";
  const auto result = send(operation, "Slow network");
  ASSERT_EQ(result.value("state", ""), "pending");
  ASSERT_EQ(transport->sends().size(), 1U);
  const auto record = transport->sends()[0];
  transport->emit_update(1, td_api::make_object<td_api::updateNewMessage>(FakeTransport::pending_message(record)));
  eventually([&] { return status(operation).contains("temporary_message_id"); });
  ASSERT_TRUE(transport->complete_send(record.temporary_id, 9100));
  eventually([&] { return status(operation).value("state", "") == "sent"; });
  ASSERT_EQ(transport->sends().size(), 1U);
}

TEST_F(MessageSendTest, OperationsAreForgottenWithTheSessionAndUnknownIdsAreNotFound) {
  ASSERT_EQ(status("b1112233-4455-4677-8899-aabbccddeeff").value("state", ""), "not_found");
  transport->set_auto_complete(true);
  send("b2112233-4455-4677-8899-aabbccddeeff", "Before logout");
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateWaitPhoneNumber>());
  eventually([&] { return runtime->snapshot(account).value("authorization_state", "") == "awaiting_phone_number"; });
  ASSERT_EQ(status("b2112233-4455-4677-8899-aabbccddeeff").value("state", ""), "not_found");
}

TEST_F(MessageSendTest, ChatsCarryAWriteHintDerivedFromTypeMembershipAndPermissions) {
  const auto hint = [&](std::int64_t chat) {
    auto value = runtime->chat(account, chat)["item"]["can_send"];
    if (value["reason"].is_null())
      value["reason"] = "";
    return value;
  };
  ASSERT_EQ(hint(42).value("text", false), true);
  transport->emit_update(
      1, td_api::make_object<td_api::updateNewChat>(make_chat(43, td_api::make_object<td_api::chatTypeSecret>(3, 8))));
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(
                                make_chat(44, td_api::make_object<td_api::chatTypeSupergroup>(10, true))));
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(
                                make_chat(45, td_api::make_object<td_api::chatTypeSupergroup>(11, false))));
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(
                                make_chat(46, td_api::make_object<td_api::chatTypeBasicGroup>(12))));
  eventually([&] { return runtime->chat(account, 46).contains("item"); });
  ASSERT_EQ(hint(43).value("reason", ""), "secret_chat");
  ASSERT_TRUE(hint(44)["text"].is_null());
  auto channel = td_api::make_object<td_api::supergroup>();
  channel->id_ = 10;
  channel->is_channel_ = true;
  channel->status_ = td_api::make_object<td_api::chatMemberStatusMember>(0);
  transport->emit_update(1, td_api::make_object<td_api::updateSupergroup>(std::move(channel)));
  auto group = td_api::make_object<td_api::supergroup>();
  group->id_ = 11;
  group->status_ = td_api::make_object<td_api::chatMemberStatusMember>(0);
  transport->emit_update(1, td_api::make_object<td_api::updateSupergroup>(std::move(group)));
  auto basic = td_api::make_object<td_api::basicGroup>();
  basic->id_ = 12;
  basic->status_ = td_api::make_object<td_api::chatMemberStatusLeft>();
  transport->emit_update(1, td_api::make_object<td_api::updateBasicGroup>(std::move(basic)));
  eventually([&] { return hint(46).value("reason", "") == "not_member"; });
  ASSERT_EQ(hint(44).value("reason", ""), "read_only");
  ASSERT_EQ(hint(45).value("text", false), true);
  auto closed = td_api::make_object<td_api::chatPermissions>();
  closed->can_send_basic_messages_ = false;
  const auto cursor = runtime->updates(account, "", 100).value("cursor", "");
  transport->emit_update(1, td_api::make_object<td_api::updateChatPermissions>(45, std::move(closed)));
  eventually([&] { return hint(45).value("reason", "") == "restricted"; });
  ASSERT_EQ(events(*runtime, account, cursor, "chat_changed").back().value("field", ""), "permissions");
  auto deleted = td_api::make_object<td_api::user>();
  deleted->id_ = 7;
  deleted->type_ = td_api::make_object<td_api::userTypeDeleted>();
  transport->emit_update(1, td_api::make_object<td_api::updateUser>(std::move(deleted)));
  eventually([&] { return hint(42).value("reason", "") == "user_deleted"; });
}

TEST_F(MessageSendTest, ConnectionChangesAreJournalledOnlyWhenTheyChange) {
  const auto cursor = runtime->updates(account, "", 100).value("cursor", "");
  transport->emit_update(
      1, td_api::make_object<td_api::updateConnectionState>(td_api::make_object<td_api::connectionStateConnecting>()));
  transport->emit_update(
      1, td_api::make_object<td_api::updateConnectionState>(td_api::make_object<td_api::connectionStateConnecting>()));
  transport->emit_update(
      1, td_api::make_object<td_api::updateConnectionState>(td_api::make_object<td_api::connectionStateReady>()));
  eventually([&] { return events(*runtime, account, cursor, "connection_changed").size() == 2; });
  const auto changes = events(*runtime, account, cursor, "connection_changed");
  ASSERT_EQ(changes[0].value("connection", ""), "connecting");
  ASSERT_EQ(changes[1].value("connection", ""), "ready");
}
