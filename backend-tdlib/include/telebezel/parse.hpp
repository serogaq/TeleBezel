#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace telebezel {
// Accepts a value only when the whole string is consumed and it fits the target
// type; overflow, suffixes and stray signs are rejected.
template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  if (text.empty())
    return std::nullopt;
  if (text.front() == '+')
    return std::nullopt;
  Integer value{};
  const auto *const first = text.data();
  const auto *const last = text.data() + text.size();
  const auto result = std::from_chars(first, last, value);
  if (result.ec != std::errc{} || result.ptr != last)
    return std::nullopt;
  return value;
}
inline std::optional<std::int64_t> parse_int64(std::string_view text) { return parse_integer<std::int64_t>(text); }
inline std::optional<std::uint64_t> parse_uint64(std::string_view text) { return parse_integer<std::uint64_t>(text); }
} // namespace telebezel
