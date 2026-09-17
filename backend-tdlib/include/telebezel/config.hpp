#pragma once
#include <cstdint>
#include <string>

namespace telebezel {
enum class ProxyMode { direct, socks5, http, mtproto };
struct ProxyConfig {
  ProxyMode mode{ProxyMode::direct};
  std::string host;
  std::uint16_t port{0};
  std::string username;
  std::string password;
  std::string secret;
};
struct Config {
  std::string listen_address{"0.0.0.0"};
  std::uint16_t listen_port{8081};
  std::string internal_token;
  std::string data_directory{"/var/lib/telebezel/tdlib"};
  ProxyConfig proxy;
};
ProxyMode parse_proxy_mode(const std::string &value);
Config load_config();
} // namespace telebezel
