#include "fakes/fake_registry.hpp"
#include "fakes/fake_transport.hpp"
#include "telebezel/http_server.hpp"
#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <td/telegram/td_api.h>
#include <thread>
#include <unistd.h>

namespace {
namespace td_api = td::td_api;

struct Reply {
  int status{0};
  std::string body;
};

Reply exchange(int port, const std::string &method, const std::string &target, const std::string &token,
               const std::string &body = {}, int timeout_seconds = 15) {
  const int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
  if (descriptor < 0)
    return {};
  const timeval timeout{timeout_seconds, 0};
  ::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  Reply reply;
  if (::connect(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0) {
    std::string request = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
    if (!token.empty())
      request += "Authorization: Bearer " + token + "\r\n";
    if (!body.empty())
      request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    std::size_t sent = 0;
    while (sent < request.size()) {
      const auto amount = ::send(descriptor, request.data() + sent, request.size() - sent, 0);
      if (amount <= 0)
        break;
      sent += static_cast<std::size_t>(amount);
    }
    std::string response;
    char buffer[4096];
    while (true) {
      const auto amount = ::recv(descriptor, buffer, sizeof(buffer), 0);
      if (amount <= 0)
        break;
      response.append(buffer, static_cast<std::size_t>(amount));
    }
    const auto separator = response.find("\r\n\r\n");
    if (response.starts_with("HTTP/1.1 ") && separator != std::string::npos) {
      reply.status = std::stoi(response.substr(9, 3));
      reply.body = response.substr(separator + 4);
    }
  }
  ::close(descriptor);
  return reply;
}

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
      return exchange(port, "PUT", interest_path(100 + index), config.internal_token, body.dump()).status;
    }));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto started = std::chrono::steady_clock::now();
  const auto health = exchange(port, "GET", "/healthz", {}, {}, 1);
  ASSERT_EQ(health.status, 200);
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
    ASSERT_EQ(exchange(port, "PUT", interest_path(42), config.internal_token, principal.dump()).status, 422)
        << principal.dump();
  }
  const auto read = exchange(port, "GET",
                             "/internal/v1/accounts/" + uuid +
                                 "/chats/42?principal_type=device&principal_id=a:b&view_id=90112233-4455-4677-8899-"
                                 "aabbccddeeff",
                             config.internal_token);
  ASSERT_EQ(read.status, 422);
}
} // namespace
