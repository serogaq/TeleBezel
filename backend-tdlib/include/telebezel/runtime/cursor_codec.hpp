#pragma once
#include "telebezel/runtime/account_state.hpp"
#include <optional>
namespace telebezel::runtime {
class CursorCodec final {
public:
  explicit CursorCodec(std::string key) : key_(std::move(key)) {}
  std::string encode(const AccountState &account, std::uint64_t sequence) const;
  std::optional<std::uint64_t> decode(const AccountState &account, const std::string &cursor) const;

private:
  std::string key_;
};
} // namespace telebezel::runtime
