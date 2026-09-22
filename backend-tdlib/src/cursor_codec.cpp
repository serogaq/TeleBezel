#include "telebezel/runtime/cursor_codec.hpp"
#include "telebezel/crypto.hpp"
#include "telebezel/parse.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/status.hpp"
namespace telebezel::runtime {
namespace {
constexpr const char *purpose = "telebezel/cursor/updates/v1";
}
std::string CursorCodec::encode(const Account &account, std::uint64_t sequence) const {
  const std::string payload =
      std::to_string(account.authorization_generation) + ":" + account.runtime_epoch + ":" + std::to_string(sequence);
  return payload + "." + hmac_sha256_hex(key_, purpose, account.uuid + ":" + account.generation + ":" + payload);
}
std::optional<std::uint64_t> CursorCodec::decode(const Account &account, const std::string &cursor) const {
  const auto dot = cursor.rfind('.');
  if (dot == std::string::npos)
    return std::nullopt;
  const auto payload = cursor.substr(0, dot);
  const auto signature = hmac_sha256_hex(key_, purpose, account.uuid + ":" + account.generation + ":" + payload);
  const auto prefix = std::to_string(account.authorization_generation) + ":" + account.runtime_epoch + ":";
  if (!token_matches(signature, cursor.substr(dot + 1)) || !payload.starts_with(prefix))
    return std::nullopt;
  return parse_uint64(std::string_view(payload).substr(prefix.size()));
}

} // namespace telebezel::runtime
