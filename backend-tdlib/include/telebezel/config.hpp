#pragma once
#include <cstdint>
#include <string>

namespace telebezel {
enum class ProxyMode { inherit, direct, socks5, http, mtproto };
struct ProxyConfig {
  ProxyMode mode{ProxyMode::direct};
  std::string host;
  std::uint16_t port{0};
  std::string username;
  std::string password;
  std::string secret;
  bool http_only{false};
};
struct Config {
  std::string listen_address{"0.0.0.0"};
  std::uint16_t listen_port{8081};
  std::string internal_token;
  std::string data_directory{"/var/lib/telebezel/tdlib"};
  std::string master_key_file;
  std::string telegram_api_hash;
  std::int32_t telegram_api_id{0};
  bool use_test_dc{false};
  ProxyConfig proxy;
};
ProxyMode parse_proxy_mode(const std::string &value);
Config load_config();
} // namespace telebezel
