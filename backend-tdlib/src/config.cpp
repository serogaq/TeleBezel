#include "telebezel/config.hpp"
#include <cstdlib>
#include <fstream>
#include <stdexcept>

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
} // namespace
ProxyMode parse_proxy_mode(const std::string &value) {
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
  if (config.proxy.mode == ProxyMode::direct &&
      (!config.proxy.host.empty() || !env_or("TDLIB_PROXY_PORT").empty() || !config.proxy.username.empty() ||
       !config.proxy.password.empty() || !config.proxy.secret.empty())) {
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
  return config;
}
} // namespace telebezel
