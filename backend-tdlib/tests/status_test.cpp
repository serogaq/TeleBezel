#include "telebezel/config.hpp"
#include "telebezel/status.hpp"
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("test assertion failed");
  }
}
} // namespace

int main() {
  using telebezel::ProxyMode;
  require(telebezel::parse_proxy_mode("direct") == ProxyMode::direct);
  require(telebezel::parse_proxy_mode("socks5") == ProxyMode::socks5);
  require(telebezel::parse_proxy_mode("http") == ProxyMode::http);
  require(telebezel::parse_proxy_mode("mtproto") == ProxyMode::mtproto);
  bool rejected = false;
  try {
    static_cast<void>(telebezel::parse_proxy_mode("invalid"));
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  require(telebezel::token_matches("secret-a", "secret-a"));
  require(!telebezel::token_matches("secret-a", "secret-b"));
  telebezel::StatusSnapshot status{true, "1.8.67", 0};
  const std::string json = telebezel::status_json(status, "request-id");
  require(json.find("\"status\":\"ready\"") != std::string::npos);
  require(json.find("\"accounts\":0") != std::string::npos);
  for (const auto &fixture : {"tdlib_status_ready.json", "tdlib_status_not_ready.json"}) {
    std::ifstream stream(std::string(TELEBEZEL_CONTRACT_FIXTURE_DIR) + "/" + fixture);
    require(static_cast<bool>(stream));
    nlohmann::json expected;
    stream >> expected;
    const bool is_ready = std::string(fixture) == "tdlib_status_ready.json";
    telebezel::StatusSnapshot snapshot{is_ready, is_ready ? "1.8.67" : "", 0};
    require(nlohmann::json::parse(telebezel::status_json(snapshot, "fixture-request-id")) == expected);
  }
  setenv("TDLIB_INTERNAL_TOKEN", "0123456789abcdef0123456789abcdef", 1);
  setenv("TDLIB_LISTEN_PORT", "8081junk", 1);
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  unsetenv("TDLIB_LISTEN_PORT");
  setenv("TDLIB_INTERNAL_TOKEN_FILE", "/nonexistent-telebezel-secret", 1);
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  unsetenv("TDLIB_INTERNAL_TOKEN_FILE");
  setenv("TDLIB_PROXY_MODE", "socks5", 1);
  setenv("TDLIB_PROXY_HOST", "proxy.example", 1);
  setenv("TDLIB_PROXY_PORT", "1080", 1);
  require(telebezel::load_config().proxy.port == 1080);
  setenv("TDLIB_PROXY_MODE", "mtproto", 1);
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  setenv("TDLIB_PROXY_SECRET", "not-logged-secret", 1);
  require(telebezel::load_config().proxy.mode == ProxyMode::mtproto);
  unsetenv("TDLIB_PROXY_SECRET");
  unsetenv("TDLIB_PROXY_MODE");
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  return 0;
}
