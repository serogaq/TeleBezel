#include "telebezel/crypto.hpp"
#include "telebezel/file_descriptor.hpp"
#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/sha.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace telebezel {
namespace {
std::uint8_t nibble(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(value - 'a' + 10);
  }
  throw std::runtime_error("invalid hexadecimal value");
}
std::array<std::uint8_t, 32> derive_key(const std::array<std::uint8_t, 32> &master_key, const std::string &uuid,
                                        const char *salt, std::size_t salt_size) {
  if (salt_size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("key derivation salt is too large");
  }
  const auto info = uuid_bytes(uuid);
  std::array<std::uint8_t, 32> output{};
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> owner(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr),
                                                                    EVP_PKEY_CTX_free);
  EVP_PKEY_CTX *context = owner.get();
  if (context == nullptr || EVP_PKEY_derive_init(context) <= 0 ||
      EVP_PKEY_CTX_set_hkdf_md(context, EVP_sha256()) <= 0 ||
      EVP_PKEY_CTX_set1_hkdf_salt(context, reinterpret_cast<const unsigned char *>(salt),
                                  static_cast<int>(salt_size)) <= 0 ||
      EVP_PKEY_CTX_set1_hkdf_key(context, master_key.data(), static_cast<int>(master_key.size())) <= 0 ||
      EVP_PKEY_CTX_add1_hkdf_info(context, info.data(), static_cast<int>(info.size())) <= 0) {
    throw std::runtime_error("key derivation failed");
  }
  std::size_t size = output.size();
  if (EVP_PKEY_derive(context, output.data(), &size) <= 0 || size != output.size()) {
    throw std::runtime_error("key derivation failed");
  }
  return output;
}
} // namespace

std::array<std::uint8_t, 32> read_master_key(const std::filesystem::path &path) {
  FileDescriptor owner(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  const int descriptor = owner.get();
  struct stat status{};
  // Compose bind-mounted secrets retain host ownership on native Linux. The
  // process must be able to open the file (typically through a named ACL), but
  // the key must never be writable by group/other or readable by everyone.
  if (descriptor < 0 || ::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) || (status.st_mode & 0037) != 0) {
    throw std::runtime_error("database master key is missing or invalid");
  }
  std::array<char, 66> buffer{};
  std::size_t length = 0;
  while (length < buffer.size()) {
    const auto amount = ::read(descriptor, buffer.data() + length, buffer.size() - length);
    if (amount > 0) {
      length += static_cast<std::size_t>(amount);
      continue;
    }
    if (amount < 0 && errno == EINTR)
      continue;
    if (amount < 0)
      length = buffer.size();
    break;
  }
  owner.close();
  if (length != 64 && !(length == 65 && buffer[64] == '\n')) {
    throw std::runtime_error("database master key is missing or invalid");
  }
  const std::string encoded(buffer.data(), 64);
  std::array<std::uint8_t, 32> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] = static_cast<std::uint8_t>((nibble(encoded[index * 2]) << 4U) | nibble(encoded[index * 2 + 1]));
  }
  return result;
}

std::array<std::uint8_t, 16> uuid_bytes(const std::string &uuid) {
  if (uuid.size() != 36 || uuid[8] != '-' || uuid[13] != '-' || uuid[18] != '-' || uuid[23] != '-') {
    throw std::runtime_error("invalid UUID");
  }
  std::array<std::uint8_t, 16> result{};
  std::size_t output = 0;
  for (std::size_t index = 0; index < uuid.size();) {
    if (uuid[index] == '-') {
      ++index;
      continue;
    }
    if (index + 1 >= uuid.size() || output >= result.size()) {
      throw std::runtime_error("invalid UUID");
    }
    result[output++] = static_cast<std::uint8_t>((nibble(uuid[index]) << 4U) | nibble(uuid[index + 1]));
    index += 2;
  }
  if (output != result.size()) {
    throw std::runtime_error("invalid UUID");
  }
  return result;
}

std::array<std::uint8_t, 32> derive_database_key(const std::array<std::uint8_t, 32> &master_key,
                                                 const std::string &uuid) {
  constexpr char salt[] = "TeleBezel/TDLib/HKDF-SHA256/v1";
  return derive_key(master_key, uuid, salt, sizeof(salt) - 1);
}

std::array<std::uint8_t, 32> derive_proxy_key(const std::array<std::uint8_t, 32> &master_key, const std::string &uuid) {
  constexpr char salt[] = "TeleBezel/Proxy/HKDF-SHA256/v1";
  return derive_key(master_key, uuid, salt, sizeof(salt) - 1);
}

std::string hex_encode(const std::uint8_t *data, std::size_t size) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(size * 2, '0');
  for (std::size_t index = 0; index < size; ++index) {
    result[index * 2] = digits[data[index] >> 4U];
    result[index * 2 + 1] = digits[data[index] & 0x0fU];
  }
  return result;
}
std::string sha256_hex(const std::string &value) {
  std::array<std::uint8_t, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char *>(value.data()), value.size(), digest.data());
  return hex_encode(digest.data(), digest.size());
}
} // namespace telebezel
