#include "telebezel/config.hpp"
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace telebezel {
namespace {
std::string env_or(const char *name, const std::string &fallback = {}) {
  const char *value = std::getenv(name);
  return value ? value : fallback;
}
std::string secret_value(const char *value_name, const char *file_name) {
  const std::string filename = env_or(file_name);
  if (!filename.empty()) {
    std::ifstream stream(filename);
    if (!stream) {
      throw std::runtime_error(std::string("Unable to read secret file for ") + value_name);
    }
    std::string value;
    std::getline(stream, value);
    return value;
  }
  return env_or(value_name);
}
std::uint16_t port_value(const std::string &value, std::uint16_t fallback) {
  if (value.empty()) {
    return fallback;
  }
  if (value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("Port must be a decimal integer");
  }
  std::size_t parsed = 0;
  unsigned long port = 0;
  try {
    port = std::stoul(value, &parsed);
  } catch (const std::exception &) {
    throw std::runtime_error("Port must be a decimal integer");
  }
  if (parsed != value.size()) {
    throw std::runtime_error("Port must be a decimal integer");
  }
  if (port == 0 || port > 65535) {
    throw std::runtime_error("Port must be between 1 and 65535");
  }
  return static_cast<std::uint16_t>(port);
}
bool bool_value(const std::string &value, bool fallback = false) {
  if (value.empty()) {
    return fallback;
  }
  if (value == "true" || value == "1") {
    return true;
  }
  if (value == "false" || value == "0") {
    return false;
  }
  throw std::runtime_error("Boolean configuration must be true or false");
}
std::int32_t api_id_value(const std::string &value) {
  if (value.empty()) {
    return 0;
  }
  if (value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("Telegram API ID must be a positive integer");
  }
  const auto result = std::stoll(value);
  if (result <= 0 || result > INT32_MAX) {
    throw std::runtime_error("Telegram API ID must be a positive integer");
  }
  return static_cast<std::int32_t>(result);
}
} // namespace
ProxyMode parse_proxy_mode(const std::string &value) {
  if (value == "inherit") {
    return ProxyMode::inherit;
  }
  if (value.empty() || value == "direct") {
    return ProxyMode::direct;
  }
  if (value == "socks5") {
    return ProxyMode::socks5;
  }
  if (value == "http") {
    return ProxyMode::http;
  }
  if (value == "mtproto") {
    return ProxyMode::mtproto;
  }
  throw std::runtime_error("Unsupported proxy mode");
}
Config load_config() {
  Config config;
  config.listen_address = env_or("TDLIB_LISTEN_ADDRESS", config.listen_address);
  config.listen_port = port_value(env_or("TDLIB_LISTEN_PORT"), config.listen_port);
  config.internal_token = secret_value("TDLIB_INTERNAL_TOKEN", "TDLIB_INTERNAL_TOKEN_FILE");
  if (config.internal_token.size() < 32) {
    throw std::runtime_error("TDLIB internal token must contain at least 32 characters");
  }
  config.data_directory = env_or("TDLIB_DATA_DIRECTORY", config.data_directory);
  config.master_key_file = env_or("TDLIB_DATABASE_MASTER_KEY_FILE");
  config.telegram_api_id = api_id_value(secret_value("TELEGRAM_API_ID", "TELEGRAM_API_ID_FILE"));
  config.telegram_api_hash = secret_value("TELEGRAM_API_HASH", "TELEGRAM_API_HASH_FILE");
  config.use_test_dc = bool_value(env_or("TDLIB_USE_TEST_DC"));
  if ((config.telegram_api_id == 0) != config.telegram_api_hash.empty()) {
    throw std::runtime_error("Telegram API ID and hash must be configured together");
  }
  config.proxy.mode = parse_proxy_mode(env_or("TDLIB_PROXY_MODE", "direct"));
  config.proxy.host = env_or("TDLIB_PROXY_HOST");
  config.proxy.port = port_value(env_or("TDLIB_PROXY_PORT"), config.proxy.mode == ProxyMode::direct ? 1 : 0);
  if (config.proxy.mode == ProxyMode::direct) {
    config.proxy.port = 0;
  }
  if (config.proxy.mode != ProxyMode::direct && config.proxy.port == 0) {
    throw std::runtime_error("Proxy port is required");
  }
  if (config.proxy.mode != ProxyMode::direct && config.proxy.host.empty()) {
    throw std::runtime_error("Proxy host is required");
  }
  config.proxy.username = secret_value("TDLIB_PROXY_USERNAME", "TDLIB_PROXY_USERNAME_FILE");
  config.proxy.password = secret_value("TDLIB_PROXY_PASSWORD", "TDLIB_PROXY_PASSWORD_FILE");
  config.proxy.secret = secret_value("TDLIB_PROXY_SECRET", "TDLIB_PROXY_SECRET_FILE");
  config.proxy.http_only = bool_value(env_or("TDLIB_PROXY_HTTP_ONLY"));
  if (config.proxy.mode == ProxyMode::direct &&
      (!config.proxy.host.empty() || !env_or("TDLIB_PROXY_PORT").empty() || !config.proxy.username.empty() ||
       !config.proxy.password.empty() || !config.proxy.secret.empty() || config.proxy.http_only)) {
    throw std::runtime_error("Direct proxy mode must not contain proxy settings");
  }
  if (config.proxy.mode == ProxyMode::mtproto &&
      (config.proxy.secret.empty() || !config.proxy.username.empty() || !config.proxy.password.empty())) {
    throw std::runtime_error("MTProto proxy requires only a secret");
  }
  if ((config.proxy.mode == ProxyMode::socks5 || config.proxy.mode == ProxyMode::http) &&
      (!config.proxy.secret.empty() || (!config.proxy.password.empty() && config.proxy.username.empty()))) {
    throw std::runtime_error("Invalid SOCKS5/HTTP proxy credential configuration");
  }
  if (config.proxy.mode != ProxyMode::http && config.proxy.http_only) {
    throw std::runtime_error("HTTP-only is valid only for an HTTP proxy");
  }
  return config;
}
} // namespace telebezel
