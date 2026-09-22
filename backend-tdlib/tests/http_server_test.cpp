#include "fakes/fake_registry.hpp"
#include "fakes/fake_transport.hpp"
#include "telebezel/http_server.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <td/telegram/td_api.h>
#include <thread>

namespace {
namespace td_api = td::td_api;

class HttpServerTest : public ::testing::Test {
protected:
  std::filesystem::path root =
      std::filesystem::temp_directory_path() / ("telebezel-http-" + telebezel::make_request_id());
  telebezel::Config config;
  std::unique_ptr<telebezel::testing::FakeRegistry> registry;
  telebezel::testing::FakeTransport *transport = nullptr;
  std::unique_ptr<telebezel::TdRuntime> runtime;
  std::unique_ptr<telebezel::HttpServer> server;
  std::thread listener;
  int port = 0;
  const std::string uuid = "00112233-4455-4677-8899-aabbccddeeff";
  const std::string device = "80112233-4455-4677-8899-aabbccddeeff";

  void SetUp() override {
    std::filesystem::create_directories(root);
    config.listen_address = "127.0.0.1";
    config.listen_port = 0;
    config.internal_token = std::string(32, 't');
    config.telegram_api_id = 12345;
    config.telegram_api_hash = "test-api-hash";
    config.master_key_file = (root / "master-key").string();
    {
      std::ofstream key(config.master_key_file);
      key << std::string(64, 'a');
    }
    std::filesystem::permissions(config.master_key_file,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
    registry = std::make_unique<telebezel::testing::FakeRegistry>(root);
    auto fake = std::make_unique<telebezel::testing::FakeTransport>();
    transport = fake.get();
    runtime = std::make_unique<telebezel::TdRuntime>(config, *registry, std::move(fake));
    runtime->start();
    const nlohmann::json command{
        {"uuid", uuid},
        {"generation", "11112233-4455-4677-8899-aabbccddeeff"},
        {"revision", 1},
        {"mode", "create"},
        {"operation_id", nullptr},
        {"lifecycle", "provisioning"},
        {"proxy", {{"id", "51112233-4455-4677-8899-aabbccddeeff"}, {"mode", "direct"}, {"http_only", false}}}};
    ASSERT_TRUE(runtime->reconcile(uuid, command).value("runtime_available", false));
    server = std::make_unique<telebezel::HttpServer>(config, *runtime);
    port = server->bind();
    ASSERT_GT(port, 0);
    listener = std::thread([this] { server->listen_bound(); });
  }
  void TearDown() override {
    server->stop();
    listener.join();
    runtime.reset();
    std::filesystem::remove_all(root);
  }
  httplib::Client client(std::time_t timeout = 15) const {
    httplib::Client result("127.0.0.1", port);
    result.set_read_timeout(timeout);
    result.set_bearer_token_auth(config.internal_token);
    return result;
  }
  std::string interest_path(std::int64_t chat_id) const {
    return "/internal/v1/accounts/" + uuid + "/chats/" + std::to_string(chat_id) +
           "/interests/90112233-4455-4677-8899-aabbccddeeff";
  }
};

TEST_F(HttpServerTest, StalledInterestRequestsDoNotBlockHealth) {
  for (int index = 0; index < 8; ++index)
    transport->queue_response(td_api::openChat::ID, nullptr);
  std::vector<std::future<int>> requests;
  for (int index = 0; index < 8; ++index) {
    requests.push_back(std::async(std::launch::async, [this, index] {
      const nlohmann::json body{{"principal_type", "device"}, {"principal_id", device}};
      const auto response = client().Put(interest_path(100 + index), body.dump(), "application/json");
      return response ? response->status : 0;
    }));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  httplib::Client health("127.0.0.1", port);
  health.set_read_timeout(1);
  const auto started = std::chrono::steady_clock::now();
  const auto response = health.Get("/healthz");
  ASSERT_TRUE(response);
  ASSERT_EQ(response->status, 200);
  ASSERT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
  int rejected = 0;
  for (auto &request : requests)
    rejected += request.get() == 503 ? 1 : 0;
  ASSERT_GE(rejected, 5) << "one account held more control slots than its limit";
}

TEST_F(HttpServerTest, InterestPrincipalMustBeAKnownTypeAndUuid) {
  for (const auto &principal :
       std::vector<nlohmann::json>{{{"principal_type", "device"}, {"principal_id", device + ":extra"}},
                                   {{"principal_type", "device"}, {"principal_id", "not-a-uuid"}},
                                   {{"principal_type", "owner"}, {"principal_id", device}},
                                   {{"principal_type", "device"}, {"principal_id", 7}}}) {
    const auto response = client().Put(interest_path(42), principal.dump(), "application/json");
    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 422) << principal.dump();
  }
  const auto read = client().Get("/internal/v1/accounts/" + uuid +
                                 "/chats/42?principal_type=device&principal_id=a:b&view_id=90112233-4455-4677-8899-"
                                 "aabbccddeeff");
  ASSERT_TRUE(read);
  ASSERT_EQ(read->status, 422);
}
} // namespace
