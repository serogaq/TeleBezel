#include "telebezel/config.hpp"
#include "telebezel/http_server.hpp"
#include "telebezel/td_runtime.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

namespace {
std::atomic<bool> stop_requested{false};
void handle_signal(int) { stop_requested.store(true); }
} // namespace

int main(int argc, char **argv) {
  try {
    const telebezel::Config config = telebezel::load_config();
    if (argc == 2 && (std::string(argv[1]) == "--healthcheck" || std::string(argv[1]) == "--readycheck" ||
                      std::string(argv[1]) == "--wrong-token-check")) {
      httplib::Client client("127.0.0.1", config.listen_port);
      client.set_connection_timeout(1);
      client.set_read_timeout(1);
      if (std::string(argv[1]) == "--healthcheck") {
        const auto response = client.Get("/healthz");
        return response && response->status == 200 ? 0 : 1;
      }
      httplib::Headers headers{
          {"Authorization",
           "Bearer " + (std::string(argv[1]) == "--wrong-token-check" ? std::string(32, 'x') : config.internal_token)}};
      const auto response = client.Get("/internal/v1/status", headers);
      if (!response) {
        return 1;
      }
      if (std::string(argv[1]) == "--wrong-token-check") {
        return response->status == 401 ? 0 : 1;
      }
      if (response->status != 200) {
        return 1;
      }
      const auto body = nlohmann::json::parse(response->body, nullptr, false);
      return !body.is_discarded() && body.value("data", nlohmann::json::object()).value("status", "") == "ready" &&
                     body["data"].value("service_version", "") == TELEBEZEL_SERVICE_VERSION &&
                     !body["data"].value("tdlib_version", "").empty()
                 ? 0
                 : 1;
    }
    telebezel::Registry registry(config.data_directory);
    registry.open();
    telebezel::TdRuntime runtime(config, registry);
    runtime.start();
    telebezel::HttpServer server(config, runtime);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::thread http_thread([&server] {
      if (!server.listen()) {
        stop_requested.store(true);
      }
    });
    while (!stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    server.stop();
    http_thread.join();
    runtime.stop();
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "backend-tdlib startup failed: " << exception.what() << '\n';
    return 1;
  }
}
