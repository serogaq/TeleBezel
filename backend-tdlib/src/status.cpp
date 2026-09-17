#include "telebezel/status.hpp"
#include <array>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <random>
#include <sstream>

namespace telebezel {
std::string make_request_id() {
  std::array<unsigned char, 16> bytes{};
  std::random_device random;
  for (auto &byte : bytes) {
    byte = static_cast<unsigned char>(random());
  }
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      output << '-';
    }
    output << std::setw(2) << static_cast<unsigned int>(bytes[i]);
  }
  return output.str();
}
std::string health_json() {
  return nlohmann::json{{"status", "ok"}, {"service", "backend-tdlib"}, {"version", TELEBEZEL_SERVICE_VERSION}}.dump();
}
std::string status_json(const StatusSnapshot &status, const std::string &request_id) {
  return nlohmann::json{{"data",
                         {{"status", status.ready ? "ready" : "not_ready"},
                          {"service_version", TELEBEZEL_SERVICE_VERSION},
                          {"tdlib_version", status.tdlib_version},
                          {"client_manager", status.ready ? "ready" : "not_ready"},
                          {"accounts", status.account_count}}},
                        {"request_id", request_id}}
      .dump();
}
bool token_matches(const std::string &expected, const std::string &provided) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> expected_hash{};
  std::array<unsigned char, SHA256_DIGEST_LENGTH> provided_hash{};
  SHA256(reinterpret_cast<const unsigned char *>(expected.data()), expected.size(), expected_hash.data());
  SHA256(reinterpret_cast<const unsigned char *>(provided.data()), provided.size(), provided_hash.data());
  return CRYPTO_memcmp(expected_hash.data(), provided_hash.data(), expected_hash.size()) == 0;
}
} // namespace telebezel
