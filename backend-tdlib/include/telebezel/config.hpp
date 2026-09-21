#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace telebezel {
enum class ProxyMode : std::uint8_t { inherit, direct, socks5, http, mtproto };
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
  std::size_t cache_messages_per_chat{500};
  std::size_t cache_messages_per_account{5000};
  std::size_t cache_messages_per_process{20000};
  std::size_t cache_projection_bytes{128ULL * 1024 * 1024};
  std::size_t preview_max_bytes{512ULL * 1024};
  std::size_t preview_total_bytes{256ULL * 1024 * 1024};
};
ProxyMode parse_proxy_mode(const std::string &value);
Config load_config();
} // namespace telebezel
