#include "fakes/fake_transport.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/runtime/account_store.hpp"
#include "telebezel/td_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <latch>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <td/telegram/td_api.h>
#include <thread>

namespace {
namespace td_api = td::td_api;

using telebezel::testing::FakeTransport;

template <class Predicate> void wait_until(Predicate predicate, int attempts = 100) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("runtime test wait timed out");
}
std::string nullable(const nlohmann::json &value, const char *key) {
  const auto found = value.find(key);
  return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}
} // namespace

TEST(CacheBudgetTest, EnforcesPerChatAccountAndProcessLimits) {
  telebezel::Config config;
  config.cache_messages_per_chat = 2;
  config.cache_messages_per_account = 3;
  config.cache_messages_per_process = 4;
  config.cache_projection_bytes = 4096;
  telebezel::runtime::AccountStore store(config);
  auto &first = store.accounts_["first"];
  auto &second = store.accounts_["second"];
  first.interest_counts[7] = 1;
  store.put_message(first, {7, 1}, {{"date", 1}, {"id", "1"}});
  store.put_message(first, {7, 2}, {{"date", 2}, {"id", "2"}});
  store.put_message(first, {7, 3}, {{"date", 3}, {"id", "3"}});
  ASSERT_FALSE(first.messages.contains({7, 1}));
  store.put_message(first, {8, 4}, {{"date", 4}, {"id", "4"}});
  store.put_message(second, {9, 5}, {{"date", 5}, {"id", "5"}});
  store.put_message(second, {9, 6}, {{"date", 6}, {"id", "6"}});
  ASSERT_EQ(first.messages.size() + second.messages.size(), 4);
  ASSERT_TRUE(first.messages.contains({7, 2}));
  ASSERT_FALSE(first.messages.contains({8, 4}));
}

class RuntimeTest : public ::testing::Test {
protected:
  std::filesystem::path root;
  telebezel::Config config;
  std::unique_ptr<telebezel::Registry> registry;
  FakeTransport *transport = nullptr;
  std::unique_ptr<telebezel::TdRuntime> runtime;
  const std::string first = "00112233-4455-4677-8899-aabbccddeeff";
  const std::string second = "10112233-4455-4677-8899-aabbccddeeff";
  const std::string third = "40112233-4455-4677-8899-aabbccddeeff";
  const std::string interrupted_logout = "50112233-4455-4677-8899-aabbccddeeff";
  const std::string interrupted_remove = "60112233-4455-4677-8899-aabbccddeeff";
  const std::string missing_proxy = "70112233-4455-4677-8899-aabbccddeeff";
  const std::string generation = "11112233-4455-4677-8899-aabbccddeeff";
  nlohmann::json command(const std::string &uuid) const {
    return {{"uuid", uuid},
            {"generation", generation},
            {"revision", 1},
            {"mode", "create"},
            {"operation_id", nullptr},
            {"lifecycle", "provisioning"},
            {"proxy", {{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}}}};
  }
  void SetUp() override {
    root = std::filesystem::temp_directory_path() / ("telebezel-runtime-" + telebezel::make_request_id());
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
    runtime = std::make_unique<telebezel::TdRuntime>(config, *registry, std::move(fake));
    runtime->start();
    ASSERT_TRUE(runtime->reconcile(first, command(first)).value("runtime_available", false));
    ASSERT_TRUE(runtime->reconcile(second, command(second)).value("runtime_available", false));
  }
  void TearDown() override {
    runtime.reset();
    registry.reset();
    std::filesystem::remove_all(root);
  }
};
TEST_F(RuntimeTest, MultiAccountReadsAndInterests) {
  ASSERT_TRUE((runtime->reconcile(first, command(first)).value("runtime_available", false)));
  ASSERT_TRUE((runtime->reconcile(second, command(second)).value("runtime_available", false)));
  ASSERT_TRUE(
      (runtime
           ->ping_proxy(first, {{"mode", "mtproto"}, {"host", "proxy.example"}, {"port", 443}, {"secret", "dd-secret"}})
           .value("latency_ms", 0) == 125));
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  auto projected_chat = td_api::make_object<td_api::chat>();
  projected_chat->id_ = 42;
  projected_chat->title_ = "Projection test";
  projected_chat->type_ = td_api::make_object<td_api::chatTypePrivate>(7);
  projected_chat->positions_.push_back(
      td_api::make_object<td_api::chatPosition>(td_api::make_object<td_api::chatListMain>(), 100, true, nullptr));
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(std::move(projected_chat)));
  wait_until([&] { return runtime->chats(first, "main", 20, "")["items"].size() == 1; });
  const auto initial_cursor = runtime->chats(first, "main", 20, "").value("updates_cursor", "");
  transport->emit_update(1, td_api::make_object<td_api::updateChatTitle>(42, "Renamed"));
  wait_until([&] { return runtime->chat(first, 42)["item"].value("title", "") == "Renamed"; });
  ASSERT_TRUE((runtime->updates(first, initial_cursor, 100)["items"].size() == 1));
  const std::string device = "80112233-4455-4677-8899-aabbccddeeff";
  const std::string view = "90112233-4455-4677-8899-aabbccddeeff";
  ASSERT_TRUE((runtime->set_interest(first, 42, "device:" + device + ":" + view + ":42", true).value("active", false)));
  ASSERT_TRUE((runtime->release_interests("device", device).value("released", 0) == 1));
  const auto sent = transport->sent();
  ASSERT_TRUE((sent.size() >= 2));
  ASSERT_TRUE((sent[0].second == td_api::setNetworkType::ID));
  ASSERT_TRUE((sent[1].second == td_api::setTdlibParameters::ID));
  auto conflicting = command(first);
  conflicting["proxy"]["id"] = "61112233-4455-4677-8899-aabbccddeeff";
  ASSERT_TRUE((runtime->reconcile(first, conflicting).value("code", "") == "operation.conflict"));
  const auto snapshots = runtime->snapshots({first, second});
  ASSERT_TRUE((snapshots["accounts"].size() == 2));
}

// An openChat TDLib never answered is undecided, not a refusal.
TEST_F(RuntimeTest, ConcurrentLeaseOpenSharesUnknownOutcome) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  transport->queue_response(td_api::openChat::ID, nullptr);
  std::latch start(2);
  const auto acquire = [&](const std::string &key) {
    start.count_down();
    start.wait();
    return runtime->set_interest(first, 42, key, true);
  };
  auto first_request = std::async(std::launch::async, acquire, "device:one:view:42");
  auto second_request = std::async(std::launch::async, acquire, "device:two:view:42");
  ASSERT_EQ(first_request.get().value("code", ""), "operation.outcome_unknown");
  ASSERT_EQ(second_request.get().value("code", ""), "operation.outcome_unknown");
  const auto sent = transport->sent();
  ASSERT_EQ(
      std::count_if(sent.begin(), sent.end(), [](const auto &item) { return item.second == td_api::openChat::ID; }), 1);
}

// If the openChat did land, dropping the interest must still close the chat.
TEST_F(RuntimeTest, UnknownOpenOutcomeStillClosesTheChat) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  transport->queue_response(td_api::openChat::ID, nullptr);
  const std::string device = "80112233-4455-4677-8899-aabbccddeeff";
  const std::string view = "90112233-4455-4677-8899-aabbccddeeff";
  const auto key = "device:" + device + ":" + view + ":42";
  ASSERT_EQ(runtime->set_interest(first, 42, key, true).value("code", ""), "operation.outcome_unknown");
  ASSERT_EQ(runtime->release_interests("device", device).value("released", 0), 1);
  wait_until(
      [&] {
        const auto sent = transport->sent();
        return std::count_if(sent.begin(), sent.end(),
                             [](const auto &item) { return item.second == td_api::closeChat::ID; }) == 1;
      },
      600);
}

TEST_F(RuntimeTest, ChatCursorTracksOrderAcrossMoreThanFiftyChats) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  for (std::int64_t id = 1; id <= 75; ++id) {
    auto chat = td_api::make_object<td_api::chat>();
    chat->id_ = id;
    chat->title_ = "Chat " + std::to_string(id);
    chat->type_ = td_api::make_object<td_api::chatTypePrivate>(id);
    chat->positions_.push_back(td_api::make_object<td_api::chatPosition>(td_api::make_object<td_api::chatListMain>(),
                                                                         1000 - id, false, nullptr));
    transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(std::move(chat)));
  }
  wait_until([&] { return runtime->chat(first, 75).contains("item"); });
  const auto first_page = runtime->chats(first, "main", 50, "");
  ASSERT_TRUE(first_page.value("has_more", false));
  const auto cursor = first_page.value("next_cursor", "");
  ASSERT_FALSE(cursor.empty());
  const auto second_page = runtime->chats(first, "main", 50, cursor);
  ASSERT_EQ(second_page["items"].size(), 25);
  transport->emit_update(1, td_api::make_object<td_api::updateChatPosition>(
                                75, td_api::make_object<td_api::chatPosition>(
                                        td_api::make_object<td_api::chatListMain>(), 2000, false, nullptr)));
  wait_until([&] { return runtime->chats(first, "main", 50, cursor).value("code", "") == "sync.resync_required"; });
}

// loadChats appends chats below the tail of the list. Those arrivals must not
// invalidate a cursor that was issued for a page above them, or paging never
// gets past the first page.
TEST_F(RuntimeTest, ChatPaginationSurvivesChatsArrivingThroughLoadChats) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  transport->script_chat_pagination(200, 25);
  std::set<std::string> seen;
  std::string cursor;
  for (int page = 0; page < 40; ++page) {
    const auto result = runtime->chats(first, "main", 20, cursor);
    ASSERT_FALSE(result.contains("code")) << result.dump();
    for (const auto &item : result["items"])
      seen.insert(item.value("id", ""));
    cursor = nullable(result, "next_cursor");
    if (cursor.empty())
      break;
  }
  ASSERT_EQ(seen.size(), 200U);
  ASSERT_TRUE(cursor.empty());
}

// Archive movement leaves a main-list cursor alone; a move above the cursor
// boundary invalidates it.
TEST_F(RuntimeTest, ChatCursorSeparatesListsAndDetectsReordering) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  for (std::int64_t id = 1; id <= 40; ++id) {
    auto chat = td_api::make_object<td_api::chat>();
    chat->id_ = id;
    chat->title_ = "Chat " + std::to_string(id);
    chat->type_ = td_api::make_object<td_api::chatTypePrivate>(id);
    chat->positions_.push_back(td_api::make_object<td_api::chatPosition>(td_api::make_object<td_api::chatListMain>(),
                                                                         1000 - id, false, nullptr));
    transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(std::move(chat)));
  }
  wait_until([&] { return runtime->chats(first, "main", 20, "")["items"].size() == 20; });
  const auto cursor = runtime->chats(first, "main", 20, "").value("next_cursor", "");
  ASSERT_FALSE(cursor.empty());
  auto archived = td_api::make_object<td_api::chat>();
  archived->id_ = 500;
  archived->title_ = "Archived";
  archived->type_ = td_api::make_object<td_api::chatTypePrivate>(500);
  archived->positions_.push_back(
      td_api::make_object<td_api::chatPosition>(td_api::make_object<td_api::chatListArchive>(), 5000, false, nullptr));
  transport->emit_update(1, td_api::make_object<td_api::updateNewChat>(std::move(archived)));
  wait_until([&] { return runtime->chat(first, 500).contains("item"); });
  ASSERT_EQ(runtime->chats(first, "main", 20, cursor)["items"].size(), 20U);
  transport->emit_update(1, td_api::make_object<td_api::updateChatPosition>(
                                40, td_api::make_object<td_api::chatPosition>(
                                        td_api::make_object<td_api::chatListMain>(), 9000, false, nullptr)));
  wait_until([&] { return runtime->chats(first, "main", 20, cursor).value("code", "") == "sync.resync_required"; });
}

// The background refresh uses the same page definition as the local read: the
// anchor is excluded, so a limit of one still asks for two messages.
TEST_F(RuntimeTest, BackgroundRefreshExcludesTheAnchorLikeTheLocalRead) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  const auto message = [](std::int64_t id) {
    auto item = td_api::make_object<td_api::message>();
    item->id_ = id;
    item->chat_id_ = 42;
    item->sender_id_ = td_api::make_object<td_api::messageSenderUser>(7);
    return item;
  };
  const auto history = [&](std::initializer_list<std::int64_t> ids) {
    auto result = td_api::make_object<td_api::messages>();
    for (const auto id : ids)
      result->messages_.push_back(message(id));
    return result;
  };
  const auto refreshed = [&](std::int64_t anchor) {
    const auto requests = transport->history_requests();
    return std::any_of(requests.begin(), requests.end(),
                       [anchor](const auto &item) { return !item.only_local && item.from_message_id == anchor; });
  };
  transport->queue_response(td_api::getChatHistory::ID, history({100}));
  transport->queue_response(td_api::getChatHistory::ID, history({100}));
  const auto cursor = nullable(runtime->messages(first, 42, 1, ""), "next_cursor");
  ASSERT_FALSE(cursor.empty());
  wait_until([&] { return refreshed(0); }, 400);
  transport->queue_response(td_api::getChatHistory::ID, history({100}));
  transport->queue_response(td_api::getChatHistory::ID, history({100, 99}));
  ASSERT_TRUE(runtime->messages(first, 42, 1, cursor)["items"].empty());
  wait_until([&] { return refreshed(100); }, 400);
  const auto requests = transport->history_requests();
  const auto refresh = std::find_if(requests.begin(), requests.end(),
                                    [](const auto &item) { return !item.only_local && item.from_message_id == 100; });
  ASSERT_NE(refresh, requests.end());
  ASSERT_EQ(refresh->limit, 2) << "the refresh gave the anchor's own slot away";
  wait_until([&] { return runtime->message(first, 42, 99)["item"].value("id", "") == "99"; }, 400);
}

// Reading the same unchanged message again must not append journal entries, or
// a client polling updates drives an endless event/GET loop.
TEST_F(RuntimeTest, RefreshWithoutChangesDoesNotGrowTheJournal) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  const auto message = [](const std::string &text) {
    auto item = td_api::make_object<td_api::message>();
    item->id_ = 77;
    item->chat_id_ = 42;
    item->sender_id_ = td_api::make_object<td_api::messageSenderUser>(7);
    auto content = td_api::make_object<td_api::messageText>();
    content->text_ = td_api::make_object<td_api::formattedText>();
    content->text_->text_ = text;
    item->content_ = std::move(content);
    return item;
  };
  transport->queue_response(td_api::getMessageLocally::ID, message("Hello"));
  transport->queue_response(td_api::getMessage::ID, message("Hello"));
  const auto cursor = nullable(runtime->message(first, 42, 77), "updates_cursor");
  ASSERT_FALSE(cursor.empty());
  for (int attempt = 0; attempt < 5; ++attempt) {
    ASSERT_EQ(runtime->message(first, 42, 77)["item"].value("id", ""), "77");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  wait_until([&] { return transport->pending_responses() == 0; }, 400);
  ASSERT_TRUE(runtime->updates(first, cursor, 100)["items"].empty()) << "an unchanged refresh published a change";
  std::this_thread::sleep_for(std::chrono::seconds(6));
  transport->queue_response(td_api::getMessage::ID, message("Edited"));
  runtime->message(first, 42, 77);
  wait_until([&] { return !runtime->updates(first, cursor, 100)["items"].empty(); }, 400);
  ASSERT_EQ(runtime->message(first, 42, 77)["item"]["content"].value("text", ""), "Edited");
}

// A finished download releases its reservation instead of holding the budget.
TEST_F(RuntimeTest, PreviewBudgetEvictsInsteadOfSaturating) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  const auto photo_message = [](std::int64_t id, std::int32_t file_id) {
    auto message = td_api::make_object<td_api::message>();
    message->id_ = id;
    message->chat_id_ = 42;
    auto content = td_api::make_object<td_api::messagePhoto>();
    content->photo_ = td_api::make_object<td_api::photo>();
    auto size = td_api::make_object<td_api::photoSize>();
    size->photo_ = td_api::make_object<td_api::file>();
    size->photo_->id_ = file_id;
    size->photo_->size_ = 100000;
    content->photo_->sizes_.push_back(std::move(size));
    message->content_ = std::move(content);
    return message;
  };
  const auto downloaded = [](std::int32_t file_id) {
    auto file = td_api::make_object<td_api::file>();
    file->id_ = file_id;
    file->size_ = 100000;
    file->local_ = td_api::make_object<td_api::localFile>();
    file->local_->is_downloading_completed_ = true;
    file->local_->downloaded_size_ = 100000;
    return file;
  };
  for (std::int32_t index = 0; index < 40; ++index) {
    auto history = td_api::make_object<td_api::messages>();
    history->messages_.push_back(photo_message(1000 + index, 200 + index));
    transport->queue_response(td_api::getChatHistory::ID, std::move(history));
    transport->queue_response(td_api::downloadFile::ID, downloaded(200 + index));
    const auto page = runtime->messages(first, 42, 1, "");
    ASSERT_NE(page["items"][0]["content"].value("preview_state", ""), "saturated")
        << "preview " << index << " was refused while the budget was free";
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// A failed download must release its reservation and become retryable.
TEST_F(RuntimeTest, FailedPreviewDownloadIsRetried) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  const auto photo_message = [] {
    auto message = td_api::make_object<td_api::message>();
    message->id_ = 66;
    message->chat_id_ = 42;
    auto content = td_api::make_object<td_api::messagePhoto>();
    content->photo_ = td_api::make_object<td_api::photo>();
    auto size = td_api::make_object<td_api::photoSize>();
    size->photo_ = td_api::make_object<td_api::file>();
    size->photo_->id_ = 31;
    size->photo_->size_ = 1024;
    content->photo_->sizes_.push_back(std::move(size));
    message->content_ = std::move(content);
    return message;
  };
  auto history = td_api::make_object<td_api::messages>();
  history->messages_.push_back(photo_message());
  transport->queue_response(td_api::getChatHistory::ID, std::move(history));
  transport->queue_response(td_api::downloadFile::ID, td_api::make_object<td_api::error>(400, "FILE_DOWNLOAD_FAILED"));
  ASSERT_EQ(runtime->messages(first, 42, 1, "")["items"][0]["content"].value("preview_state", ""), "queued");
  std::this_thread::sleep_for(std::chrono::seconds(3));
  auto retry_history = td_api::make_object<td_api::messages>();
  retry_history->messages_.push_back(photo_message());
  transport->queue_response(td_api::getChatHistory::ID, std::move(retry_history));
  auto file = td_api::make_object<td_api::file>();
  file->id_ = 31;
  file->size_ = 1024;
  file->local_ = td_api::make_object<td_api::localFile>();
  file->local_->is_downloading_completed_ = true;
  file->local_->downloaded_size_ = 1024;
  transport->queue_response(td_api::downloadFile::ID, std::move(file));
  ASSERT_EQ(runtime->messages(first, 42, 1, "")["items"][0]["content"].value("preview_state", ""), "queued")
      << "a failed download stayed pending for ever";
}

TEST_F(RuntimeTest, HistoryUsesLocalResultsAndInvalidatesAfterLogout) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  const auto history = [](std::initializer_list<std::int64_t> ids) {
    auto result = td_api::make_object<td_api::messages>();
    for (const auto id : ids) {
      auto item = td_api::make_object<td_api::message>();
      item->id_ = id;
      item->chat_id_ = 42;
      item->sender_id_ = td_api::make_object<td_api::messageSenderUser>(7);
      result->messages_.push_back(std::move(item));
    }
    result->total_count_ = static_cast<std::int32_t>(ids.size());
    return result;
  };
  transport->queue_response(td_api::getChatHistory::ID, history({100, 99}));
  const auto page = runtime->messages(first, 42, 1, "");
  ASSERT_EQ(page.value("source", ""), "tdlib_local");
  ASSERT_EQ(page["items"].size(), 1);
  ASSERT_EQ(page["items"][0].value("id", ""), "100");
  const auto cursor = page.value("next_cursor", "");
  ASSERT_FALSE(cursor.empty());
  auto logout = command(first);
  logout["revision"] = 2;
  logout["authorization_generation"] = 2;
  logout["lifecycle"] = "logout_pending";
  logout["operation_id"] = "81112233-4455-4677-8899-aabbccddeeff";
  logout["logout_operation_id"] = logout["operation_id"];
  ASSERT_TRUE(runtime->logout(first, logout).contains("completed"));
  ASSERT_EQ(runtime->messages(first, 42, 1, cursor).value("code", ""), "authorization.invalid_state");
}

TEST_F(RuntimeTest, PreviewRequiresCurrentMessageAndCompletedSmallFile) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  const auto photo_message = [] {
    auto message = td_api::make_object<td_api::message>();
    message->id_ = 55;
    message->chat_id_ = 42;
    auto content = td_api::make_object<td_api::messagePhoto>();
    content->photo_ = td_api::make_object<td_api::photo>();
    auto size = td_api::make_object<td_api::photoSize>();
    size->photo_ = td_api::make_object<td_api::file>();
    size->photo_->id_ = 17;
    size->photo_->size_ = 4;
    content->photo_->sizes_.push_back(std::move(size));
    message->content_ = std::move(content);
    return message;
  };
  auto history = td_api::make_object<td_api::messages>();
  history->messages_.push_back(photo_message());
  transport->queue_response(td_api::getChatHistory::ID, std::move(history));
  const auto page = runtime->messages(first, 42, 1, "");
  const auto preview_id = page["items"][0]["content"].value("preview_id", "");
  ASSERT_EQ(preview_id.size(), 64);
  ASSERT_FALSE(page["items"][0]["content"].contains("preview_file_id"));
  ASSERT_EQ(runtime->preview(first, 42, 55, std::string(64, '0')).value("code", ""), "message.cache_miss");
  transport->queue_response(td_api::getMessageLocally::ID, photo_message());
  auto file = td_api::make_object<td_api::file>();
  file->id_ = 17;
  file->size_ = 4;
  file->local_ = td_api::make_object<td_api::localFile>();
  file->local_->is_downloading_completed_ = true;
  transport->queue_response(td_api::getFile::ID, std::move(file));
  auto bytes = td_api::make_object<td_api::data>();
  bytes->data_ = "test";
  transport->queue_response(td_api::readFilePart::ID, std::move(bytes));
  const auto preview = runtime->preview(first, 42, 55, preview_id);
  ASSERT_EQ(preview.value("mime_type", ""), "image/jpeg");
  ASSERT_EQ(preview.value("bytes_base64", ""), "dGVzdA==");
}

TEST_F(RuntimeTest, HistoryFillsShortLocalPagesWithoutRepeatingAnchors) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  const auto history = [](std::initializer_list<std::int64_t> ids) {
    auto result = td_api::make_object<td_api::messages>();
    for (const auto id : ids) {
      auto item = td_api::make_object<td_api::message>();
      item->id_ = id;
      item->chat_id_ = 42;
      result->messages_.push_back(std::move(item));
    }
    result->total_count_ = static_cast<std::int32_t>(ids.size());
    return result;
  };
  transport->queue_response(td_api::getChatHistory::ID, history({100}));
  transport->queue_response(td_api::getChatHistory::ID, history({100, 99}));
  transport->queue_response(td_api::getChatHistory::ID, history({99, 98}));
  const auto page = runtime->messages(first, 42, 3, "");
  ASSERT_EQ(page["items"].size(), 3);
  ASSERT_EQ(page["items"][0].value("id", ""), "100");
  ASSERT_EQ(page["items"][1].value("id", ""), "99");
  ASSERT_EQ(page["items"][2].value("id", ""), "98");
  ASSERT_FALSE(page.value("partial", true));
}

TEST_F(RuntimeTest, ExternalAuthorizationChangePersistsGeneration) {
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_state", "") == "ready"; });
  transport->emit_state(1, td_api::make_object<td_api::authorizationStateLoggingOut>());
  wait_until([&] { return runtime->snapshot(first).value("authorization_generation", 0) == 2; });
  wait_until([&] { return registry->read(first)->authorization_generation == 2; });
  auto stale = command(first);
  stale["authorization_generation"] = 1;
  ASSERT_THROW(runtime->reconcile(first, stale), std::runtime_error);
  auto current = command(first);
  current["authorization_generation"] = 2;
  ASSERT_TRUE(runtime->reconcile(first, current).value("runtime_available", false));
  runtime->stop();
  auto restarted_transport = std::make_unique<FakeTransport>();
  transport = restarted_transport.get();
  runtime = std::make_unique<telebezel::TdRuntime>(config, *registry, std::move(restarted_transport));
  runtime->start();
  ASSERT_EQ(runtime->snapshot(first).value("authorization_generation", 0), 2);
  ASSERT_FALSE(runtime->snapshot(first).value("runtime_available", true));
}

TEST_F(RuntimeTest, AuthorizationStatesAndActions) {
  auto stale_action = command(second);
  stale_action["action"] = "submit_phone_number";
  stale_action["authorization_version"] = "stale";
  stale_action["value"] = "+15550000000";
  ASSERT_TRUE((runtime->authorization_action(second, stale_action).value("code", "") == "authorization.invalid_state"));
  const auto assert_state = [&](td_api::object_ptr<td_api::AuthorizationState> state, const std::string &expected) {
    transport->emit_state(2, std::move(state));
    wait_until([&] { return runtime->snapshot(second).value("authorization_state", "") == expected; });
  };
  const auto perform_action = [&](const std::string &action, const std::optional<std::string> &value,
                                  std::int32_t expected_function) {
    auto action_command = command(second);
    action_command["action"] = action;
    action_command["authorization_version"] =
        runtime->snapshot(second)["authorization"].value("authorization_version", "");
    if (value)
      action_command["value"] = *value;
    ASSERT_TRUE((!runtime->authorization_action(second, action_command).value("_error", false)));
    ASSERT_TRUE((transport->sent().back().second == expected_function));
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
  ASSERT_TRUE((runtime->snapshot(second).value("last_error_code", "") == "authorization.unsupported_state"));
  transport->emit_state(
      2, td_api::make_object<td_api::authorizationStateWaitCode>(td_api::make_object<td_api::authenticationCodeInfo>(
             "+15550000000", td_api::make_object<td_api::authenticationCodeTypeSms>(6),
             td_api::make_object<td_api::authenticationCodeTypeCall>(6), 0)));
  wait_until([&] { return runtime->snapshot(second).value("authorization_state", "") == "awaiting_code"; });
  auto authorization = runtime->snapshot(second)["authorization"];
  ASSERT_TRUE((authorization.value("delivery_method", "") == "sms"));
  ASSERT_TRUE((authorization.value("resend_available", false)));
  ASSERT_TRUE((std::find(authorization["allowed_actions"].begin(), authorization["allowed_actions"].end(),
                         "resend_code") != authorization["allowed_actions"].end()));
  perform_action("submit_code", "001234", td_api::checkAuthenticationCode::ID);
  auto resend = command(second);
  resend["action"] = "resend_code";
  resend["authorization_version"] = authorization["authorization_version"];
  ASSERT_TRUE((!runtime->authorization_action(second, resend).value("_error", false)));
  ASSERT_TRUE((transport->sent().back().second == td_api::resendAuthenticationCode::ID));
  transport->emit_state(2, td_api::make_object<td_api::authorizationStateWaitOtherDeviceConfirmation>("tg://login"));
  wait_until(
      [&] { return runtime->snapshot(second)["authorization"].value("state", "") == "awaiting_qr_confirmation"; });
  ASSERT_TRUE((runtime->snapshot(second)["authorization"].value("qr_link", "") == "tg://login"));
  transport->emit_state(2,
                        td_api::make_object<td_api::authorizationStateWaitOtherDeviceConfirmation>("tg://refreshed"));
  wait_until([&] { return runtime->snapshot(second)["authorization"].value("qr_link", "") == "tg://refreshed"; });
  transport->emit_state(2, td_api::make_object<td_api::authorizationStateReady>());
  wait_until([&] { return runtime->snapshot(second).contains("telegram_identity"); });
  const auto identity = runtime->snapshot(second)["telegram_identity"];
  ASSERT_TRUE((identity.value("id", "") == "9007199254740000"));
  ASSERT_TRUE((identity.value("first_name", "") == "Ada"));
  ASSERT_TRUE((!identity.contains("phone_number")));
}

TEST_F(RuntimeTest, LogoutAndProxyRevisions) {
  auto logout = command(second);
  logout["revision"] = 2;
  logout["authorization_generation"] = 2;
  logout["lifecycle"] = "logout_pending";
  logout["operation_id"] = "31112233-4455-4677-8899-aabbccddeeff";
  logout["logout_operation_id"] = logout["operation_id"];
  ASSERT_TRUE((!runtime->logout(second, logout).value("completed", false)));
  ASSERT_EQ(runtime->snapshot(second).value("target_revision", 0), 2);
  ASSERT_EQ(runtime->snapshot(second).value("applied_revision", 0), 1);
  ASSERT_EQ(registry->read(second)->authorization_generation, 2);
  wait_until([&] { return runtime->snapshot(second).value("authorization_state", "") == "closed"; });
  ASSERT_TRUE((runtime->logout(second, logout).value("completed", false)));
  ASSERT_EQ(runtime->snapshot(second).value("applied_revision", 0), 2);
  auto proxy_update = logout;
  proxy_update["revision"] = 3;
  proxy_update["effective_config_id"] = "51112233-4455-4677-8899-aabbccddeeff";
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
  ASSERT_TRUE((runtime->update_proxy(second, proxy_update).value("completed", false)));
  ASSERT_TRUE((runtime->snapshot(second).value("effective_config_id", "") ==
               proxy_update["effective_config_id"].get<std::string>()));
  auto direct_update = proxy_update;
  direct_update["revision"] = 4;
  direct_update["effective_config_id"] = "61112233-4455-4677-8899-aabbccddeeff";
  direct_update["operation_id"] = "61112233-4455-4677-8899-aabbccddeefe";
  direct_update["proxy"] = nlohmann::json{{"mode", "direct"}};
  const auto sends_before = transport->sent();
  const auto closes_before = std::count_if(sends_before.begin(), sends_before.end(),
                                           [](const auto &sent) { return sent.second == td_api::close::ID; });
  ASSERT_TRUE((runtime->update_proxy(second, direct_update).value("completed", false)));
  const auto sends_after = transport->sent();
  const auto closes_after = std::count_if(sends_after.begin(), sends_after.end(),
                                          [](const auto &sent) { return sent.second == td_api::close::ID; });
  ASSERT_EQ(closes_after, closes_before);
  ASSERT_TRUE((runtime->snapshot(second).value("effective_config_id", "") ==
               direct_update["effective_config_id"].get<std::string>()));
  auto stale_proxy = proxy_update;
  stale_proxy["revision"] = 2;
  bool stale_proxy_rejected = false;
  try {
    static_cast<void>(runtime->update_proxy(second, stale_proxy));
  } catch (const std::runtime_error &error) {
    stale_proxy_rejected = std::string(error.what()) == "operation.conflict";
  }
  ASSERT_TRUE((stale_proxy_rejected));
}

TEST_F(RuntimeTest, FailedProxyActivationSerializesRetries) {
  auto failing_proxy = command(third);
  failing_proxy["proxy"] = nlohmann::json{{"id", "81112233-4455-4677-8899-aabbccddeeff"},
                                          {"mode", "http"},
                                          {"host", "bad-proxy.example"},
                                          {"port", 8080},
                                          {"http_only", true}};
  transport->set_fail_add_proxy(true);
  transport->set_suppress_close_updates(true);
  const auto sent_before_failure = transport->sent().size();
  auto failed_reconcile = std::async(std::launch::async, [&] { return runtime->reconcile(third, failing_proxy); });
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
  ASSERT_TRUE((runtime->reconcile(third, failing_proxy).value("code", "") == "operation.conflict"));
  transport->emit_state(closing_client, td_api::make_object<td_api::authorizationStateClosed>());
  ASSERT_TRUE((failed_reconcile.get().value("code", "") == "configuration.invalid"));
  const auto failed_activation = transport->sent();
  ASSERT_TRUE((std::count_if(failed_activation.begin() + static_cast<std::ptrdiff_t>(sent_before_failure),
                             failed_activation.end(),
                             [](const auto &item) { return item.second == td_api::setNetworkType::ID; }) == 1));
  transport->set_fail_add_proxy(false);
  transport->set_suppress_close_updates(false);
  ASSERT_TRUE((runtime->reconcile(third, failing_proxy).value("runtime_available", false)));
}

TEST_F(RuntimeTest, RemovalIsIdempotentAndTombstoned) {
  auto remove = command(first);
  remove["revision"] = 2;
  remove["lifecycle"] = "removing";
  remove["operation_id"] = "21112233-4455-4677-8899-aabbccddeeff";
  ASSERT_TRUE((runtime->remove(first, remove).value("completed", false)));
  ASSERT_TRUE((runtime->remove(first, remove).value("completed", false)));
  ASSERT_TRUE((registry->read(first)->tombstone));
  bool rejected_tombstone = false;
  try {
    static_cast<void>(runtime->reconcile(first, remove));
  } catch (const std::runtime_error &error) {
    rejected_tombstone = std::string(error.what()) == "account.gone";
  }
  ASSERT_TRUE((rejected_tombstone));
  runtime->stop();
}

TEST_F(RuntimeTest, DiscoveredLifecycleIntentsResume) {
  runtime->stop();
  registry->ensure_account_directories(interrupted_logout);
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
  registry->write(
      logout_manifest,
      nlohmann::json{{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}});
  registry->ensure_account_directories(interrupted_remove);
  telebezel::AccountManifest remove_manifest{1,          interrupted_remove,
                                             generation, false,
                                             1,          1,
                                             "removing", "91112233-4455-4677-8899-aabbccddeeff",
                                             false,      nullptr,
                                             "",         "closing"};
  registry->write(
      remove_manifest,
      nlohmann::json{{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}});

  runtime->start();
  auto resumed_logout = command(interrupted_logout);
  resumed_logout["lifecycle"] = "logout_pending";
  resumed_logout["operation_id"] = logout_manifest.operation_id;
  resumed_logout["logout_operation_id"] = logout_manifest.operation_id;
  const auto before_resumed_logout = transport->sent();
  const auto logout_requests_before = std::count_if(before_resumed_logout.begin(), before_resumed_logout.end(),
                                                    [](const auto &item) { return item.second == td_api::logOut::ID; });
  ASSERT_TRUE((!runtime->logout(interrupted_logout, resumed_logout).value("completed", false)));
  wait_until([&] { return runtime->snapshot(interrupted_logout).value("authorization_state", "") == "closed"; });
  const auto after_resumed_logout = transport->sent();
  ASSERT_TRUE((std::count_if(after_resumed_logout.begin(), after_resumed_logout.end(), [](const auto &item) {
                 return item.second == td_api::logOut::ID;
               }) == logout_requests_before + 1));
  ASSERT_TRUE((runtime->logout(interrupted_logout, resumed_logout).value("completed", false)));

  auto resumed_remove = command(interrupted_remove);
  resumed_remove["lifecycle"] = "removing";
  resumed_remove["operation_id"] = remove_manifest.operation_id;
  ASSERT_TRUE((runtime->remove(interrupted_remove, resumed_remove).value("completed", false)));
  ASSERT_TRUE((registry->read(interrupted_remove)->tombstone));
}

TEST_F(RuntimeTest, MissingProxyRequiresConfiguration) {
  auto id_only = command(missing_proxy);
  id_only["proxy"] = nlohmann::json{{"id", "a1112233-4455-4677-8899-aabbccddeeff"}};
  ASSERT_TRUE((runtime->reconcile(missing_proxy, id_only).value("code", "") == "configuration.missing"));
  id_only["proxy"] =
      nlohmann::json{{"id", "a1112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}};
  ASSERT_TRUE((runtime->reconcile(missing_proxy, id_only).value("runtime_available", false)));
}
