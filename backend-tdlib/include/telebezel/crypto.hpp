#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace telebezel {
std::array<std::uint8_t, 32> read_master_key(const std::filesystem::path &path);
std::array<std::uint8_t, 16> uuid_bytes(const std::string &uuid);
std::array<std::uint8_t, 32> derive_database_key(const std::array<std::uint8_t, 32> &master_key,
                                                 const std::string &uuid);
std::array<std::uint8_t, 32> derive_proxy_key(const std::array<std::uint8_t, 32> &master_key, const std::string &uuid);
std::string hex_encode(const std::uint8_t *data, std::size_t size);
std::string sha256_hex(const std::string &value);
// `purpose` is a domain separator: a token minted for one kind of value can
// never be replayed as another.
std::string hmac_sha256_hex(const std::string &key, const std::string &purpose, const std::string &payload);
} // namespace telebezel
