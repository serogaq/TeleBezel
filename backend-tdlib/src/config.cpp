#include "telebezel/config.hpp"
#include "telebezel/crypto.hpp"
#include "telebezel/file_descriptor.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace telebezel {
namespace {
std::string env_or(const char *name, const std::string &fallback = {}) {
  const char *value = std::getenv(name);
  return value ? value : fallback;
}
std::string secret_value(const char *value_name, const char *file_name) {
  const std::string filename = env_or(file_name);
  if (!filename.empty())
    return read_secret_file(filename, value_name);
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
std::size_t limit_value(const std::string &value, std::size_t fallback) {
  if (value.empty())
    return fallback;
  if (value.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("Cache limit must be a positive integer");
  try {
    const auto parsed = std::stoull(value);
    if (parsed == 0 || parsed > 1'000'000'000ULL)
      throw std::runtime_error("Cache limit is out of range");
    return static_cast<std::size_t>(parsed);
  } catch (const std::exception &) {
    throw std::runtime_error("Cache limit is out of range");
  }
}
std::size_t seconds_value(const std::string &value, std::size_t fallback, std::size_t maximum) {
  if (value.empty())
    return fallback;
  if (value.find_first_not_of("0123456789") != std::string::npos || value.size() > 9)
    throw std::runtime_error("Duration must be a non-negative number of seconds");
  const auto parsed = static_cast<std::size_t>(std::stoull(value));
  if (parsed > maximum)
    throw std::runtime_error("Duration is out of range");
  return parsed;
}
} // namespace
std::string read_secret_file(const std::string &path, const std::string &name) {
  const auto failure = [&name] { return std::runtime_error("Secret file for " + name + " is missing or unsafe"); };
  FileDescriptor owner(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  struct stat status{};
  if (owner.get() < 0 || ::fstat(owner.get(), &status) != 0 || !S_ISREG(status.st_mode) ||
      (status.st_mode & 0037) != 0 || status.st_size > 4096)
    throw failure();
  std::string value(static_cast<std::size_t>(status.st_size), '\0');
  std::size_t length = 0;
  while (length < value.size()) {
    const auto amount = ::read(owner.get(), value.data() + length, value.size() - length);
    if (amount < 0 && errno == EINTR)
      continue;
    if (amount <= 0)
      break;
    length += static_cast<std::size_t>(amount);
  }
  value.resize(length);
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
    value.pop_back();
  if (value.empty() || value.find_first_of("\r\n") != std::string::npos)
    throw failure();
  return value;
}
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
  config.cache_messages_per_chat = limit_value(env_or("TDLIB_CACHE_MESSAGES_PER_CHAT"), config.cache_messages_per_chat);
  config.cache_messages_per_account =
      limit_value(env_or("TDLIB_CACHE_MESSAGES_PER_ACCOUNT"), config.cache_messages_per_account);
  config.cache_messages_per_process =
      limit_value(env_or("TDLIB_CACHE_MESSAGES_PER_PROCESS"), config.cache_messages_per_process);
  config.cache_projection_bytes = limit_value(env_or("TDLIB_CACHE_PROJECTION_BYTES"), config.cache_projection_bytes);
  config.preview_max_bytes = limit_value(env_or("TDLIB_PREVIEW_MAX_BYTES"), config.preview_max_bytes);
  config.preview_total_bytes = limit_value(env_or("TDLIB_PREVIEW_TOTAL_BYTES"), config.preview_total_bytes);
  config.preview_failure_ttl_seconds =
      seconds_value(env_or("TDLIB_PREVIEW_FAILURE_TTL_SECONDS"), config.preview_failure_ttl_seconds, 86400);
  config.updates_wait_max_seconds =
      seconds_value(env_or("TDLIB_UPDATES_WAIT_MAX_SECONDS"), config.updates_wait_max_seconds, 25);
  if (config.preview_max_bytes > config.preview_total_bytes)
    throw std::runtime_error("TDLIB_PREVIEW_MAX_BYTES must not exceed TDLIB_PREVIEW_TOTAL_BYTES");
  if (config.cache_messages_per_chat > config.cache_messages_per_account ||
      config.cache_messages_per_account > config.cache_messages_per_process)
    throw std::runtime_error("Cache limits must satisfy per-chat <= per-account <= per-process");
  if (!config.master_key_file.empty()) {
    try {
      static_cast<void>(read_master_key(config.master_key_file));
    } catch (const std::exception &) {
      throw std::runtime_error("TDLIB_DATABASE_MASTER_KEY_FILE is missing or invalid");
    }
  }
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
