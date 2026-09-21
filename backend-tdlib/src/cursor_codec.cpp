#include "telebezel/runtime/cursor_codec.hpp"
#include "telebezel/crypto.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/status.hpp"
namespace telebezel::runtime {
std::string CursorCodec::encode(const Account &account, std::uint64_t sequence) const {
  const std::string payload =
      std::to_string(account.authorization_generation) + ":" + account.runtime_epoch + ":" + std::to_string(sequence);
  return payload + "." + sha256_hex(key_ + ":" + account.uuid + ":" + account.generation + ":" + payload);
}
std::optional<std::uint64_t> CursorCodec::decode(const Account &account, const std::string &cursor) const {
  const auto dot = cursor.rfind('.');
  if (dot == std::string::npos)
    return std::nullopt;
  const auto payload = cursor.substr(0, dot);
  const auto signature = sha256_hex(key_ + ":" + account.uuid + ":" + account.generation + ":" + payload);
  const auto prefix = std::to_string(account.authorization_generation) + ":" + account.runtime_epoch + ":";
  if (!token_matches(signature, cursor.substr(dot + 1)) || !payload.starts_with(prefix))
    return std::nullopt;
  try {
    std::size_t parsed = 0;
    const auto sequence = std::stoull(payload.substr(prefix.size()), &parsed);
    if (parsed != payload.size() - prefix.size())
      return std::nullopt;
    return sequence;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

} // namespace telebezel::runtime
