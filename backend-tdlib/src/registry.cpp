#include "telebezel/registry.hpp"
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace telebezel {
namespace {
void ensure_plain_directory(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    throw std::runtime_error("storage.io_error");
  }
  if (std::filesystem::exists(status)) {
    if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
      throw std::runtime_error("storage.unsafe_path");
    }
  } else if (!std::filesystem::create_directory(path)) {
    throw std::runtime_error("storage.io_error");
  }
  struct stat metadata{};
  if (::lstat(path.c_str(), &metadata) != 0 || metadata.st_uid != ::geteuid() || !S_ISDIR(metadata.st_mode) ||
      ::chmod(path.c_str(), 0700) != 0) {
    throw std::runtime_error("storage.unsafe_path");
  }
}

void sync_directory(const std::filesystem::path &path) {
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0 || ::fsync(descriptor) != 0) {
    if (descriptor >= 0) {
      ::close(descriptor);
    }
    throw std::runtime_error("storage.io_error");
  }
  ::close(descriptor);
}
} // namespace

bool valid_uuid(const std::string &value) {
  static const std::regex expression("^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
  return std::regex_match(value, expression);
}

Registry::Registry(std::filesystem::path root)
    : root_(std::move(root)), registry_root_(root_ / "registry"), accounts_root_(root_ / "accounts") {}

Registry::~Registry() {
  if (lock_fd_ >= 0) {
    ::flock(lock_fd_, LOCK_UN);
    ::close(lock_fd_);
  }
}

void Registry::open() {
  if (root_.empty() || root_ == root_.root_path()) {
    throw std::runtime_error("storage.unsafe_path");
  }
  std::error_code error;
  if (!std::filesystem::exists(root_, error)) {
    if (!std::filesystem::create_directories(root_, error) || error) {
      throw std::runtime_error("storage.io_error");
    }
  }
  ensure_plain_directory(root_);
  ensure_plain_directory(registry_root_);
  ensure_plain_directory(accounts_root_);
  const auto lock_path = root_ / ".telebezel.lock";
  lock_fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock_fd_ < 0 || ::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
    throw std::runtime_error("storage.volume_in_use");
  }
}

std::filesystem::path Registry::checked_path(const std::filesystem::path &base, const std::string &component) const {
  if (!valid_uuid(component)) {
    throw std::runtime_error("storage.unsafe_path");
  }
  const auto result = base / component;
  if (result.parent_path() != base) {
    throw std::runtime_error("storage.unsafe_path");
  }
  return result;
}

std::optional<AccountManifest> Registry::read(const std::string &uuid) const {
  const auto path = checked_path(registry_root_, uuid);
  const auto manifest_path = std::filesystem::path(path.string() + ".json");
  const auto status = std::filesystem::symlink_status(manifest_path);
  if (!std::filesystem::exists(status)) {
    return std::nullopt;
  }
  if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
    throw std::runtime_error("storage.unsafe_path");
  }
  struct stat metadata{};
  if (::lstat(manifest_path.c_str(), &metadata) != 0 || metadata.st_uid != ::geteuid() ||
      (metadata.st_mode & 0077) != 0) {
    throw std::runtime_error("storage.unsafe_path");
  }
  std::ifstream input(manifest_path);
  nlohmann::json json;
  try {
    input >> json;
  } catch (const std::exception &) {
    throw std::runtime_error("storage.corrupt");
  }
  AccountManifest result;
  try {
    result.format_version = json.at("format_version").get<std::uint32_t>();
    result.uuid = json.at("uuid").get<std::string>();
    result.generation = json.at("generation").get<std::string>();
    result.use_test_dc = json.at("use_test_dc").get<bool>();
    result.key_derivation_version = json.at("key_derivation_version").get<std::uint32_t>();
    result.revision = json.at("revision").get<std::uint64_t>();
    result.lifecycle = json.at("lifecycle").get<std::string>();
    result.operation_id = json.contains("operation_id") && json["operation_id"].is_string()
                              ? json["operation_id"].get<std::string>()
                              : std::string{};
    result.tombstone = json.value("tombstone", false);
    result.content_hash = json.contains("content_hash") && json["content_hash"].is_string()
                              ? json["content_hash"].get<std::string>()
                              : std::string{};
    result.proxy = json.at("proxy");
  } catch (const std::exception &) {
    throw std::runtime_error("storage.corrupt");
  }
  if (result.format_version != 1 || result.key_derivation_version != 1 || result.uuid != uuid ||
      !valid_uuid(result.uuid) || !valid_uuid(result.generation) || !result.proxy.is_object()) {
    throw std::runtime_error("storage.corrupt");
  }
  return result;
}

std::vector<AccountManifest> Registry::discover() const {
  std::vector<AccountManifest> result;
  for (const auto &entry : std::filesystem::directory_iterator(registry_root_)) {
    const auto status = entry.symlink_status();
    if (!entry.is_regular_file() || std::filesystem::is_symlink(status) || entry.path().extension() != ".json") {
      continue;
    }
    const auto uuid = entry.path().stem().string();
    if (valid_uuid(uuid)) {
      try {
        result.push_back(*read(uuid));
      } catch (const std::exception &) {
        // The runtime reports the isolated account through manifest_errors().
      }
    }
  }
  return result;
}

std::vector<std::string> Registry::orphan_account_ids() const {
  std::vector<std::string> result;
  for (const auto &entry : std::filesystem::directory_iterator(accounts_root_)) {
    const auto uuid = entry.path().filename().string();
    if (!valid_uuid(uuid))
      continue;
    const auto status = entry.symlink_status();
    if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
      continue;
    const auto manifest = registry_root_ / (uuid + ".json");
    if (!std::filesystem::exists(std::filesystem::symlink_status(manifest))) {
      result.push_back(uuid);
    }
  }
  return result;
}

std::map<std::string, std::string> Registry::manifest_errors() const {
  std::map<std::string, std::string> result;
  for (const auto &entry : std::filesystem::directory_iterator(registry_root_)) {
    const auto uuid = entry.path().stem().string();
    if (!valid_uuid(uuid) || entry.path().extension() != ".json")
      continue;
    try {
      static_cast<void>(read(uuid));
    } catch (const std::exception &exception) {
      const std::string code = exception.what();
      result.emplace(uuid, code == "storage.unsafe_path" ? code : "storage.corrupt");
    }
  }
  return result;
}

void Registry::write(const AccountManifest &manifest, const nlohmann::json &proxy) {
  if (!valid_uuid(manifest.uuid) || !valid_uuid(manifest.generation)) {
    throw std::runtime_error("storage.identity_mismatch");
  }
  const auto base = checked_path(registry_root_, manifest.uuid);
  const auto destination = std::filesystem::path(base.string() + ".json");
  const auto temporary = std::filesystem::path(base.string() + ".tmp");
  for (const auto &path : {destination, temporary}) {
    const auto status = std::filesystem::symlink_status(path);
    if (std::filesystem::exists(status) &&
        (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))) {
      throw std::runtime_error("storage.unsafe_path");
    }
  }
  const nlohmann::json json{
      {"format_version", manifest.format_version},
      {"uuid", manifest.uuid},
      {"generation", manifest.generation},
      {"use_test_dc", manifest.use_test_dc},
      {"key_derivation_version", manifest.key_derivation_version},
      {"revision", manifest.revision},
      {"lifecycle", manifest.lifecycle},
      {"operation_id", manifest.operation_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(manifest.operation_id)},
      {"tombstone", manifest.tombstone},
      {"content_hash", manifest.content_hash.empty() ? nlohmann::json(nullptr) : nlohmann::json(manifest.content_hash)},
      {"proxy", proxy}};
  const std::string encoded = json.dump();
  const int descriptor = ::open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor < 0) {
    throw std::runtime_error("storage.io_error");
  }
  std::size_t written = 0;
  while (written < encoded.size()) {
    const auto amount = ::write(descriptor, encoded.data() + written, encoded.size() - written);
    if (amount <= 0) {
      ::close(descriptor);
      throw std::runtime_error("storage.io_error");
    }
    written += static_cast<std::size_t>(amount);
  }
  if (::fsync(descriptor) != 0 || ::close(descriptor) != 0 || ::rename(temporary.c_str(), destination.c_str()) != 0) {
    throw std::runtime_error("storage.io_error");
  }
  sync_directory(registry_root_);
}

void Registry::ensure_account_directories(const std::string &uuid) {
  const auto account = checked_path(accounts_root_, uuid);
  if (!std::filesystem::exists(account)) {
    ensure_plain_directory(account);
  } else {
    ensure_plain_directory(account);
  }
  ensure_plain_directory(account / "db");
  ensure_plain_directory(account / "files");
}

bool Registry::account_directory_exists(const std::string &uuid) const {
  const auto path = checked_path(accounts_root_, uuid);
  const auto status = std::filesystem::symlink_status(path);
  if (!std::filesystem::exists(status)) {
    return false;
  }
  if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
    throw std::runtime_error("storage.unsafe_path");
  }
  return true;
}

void Registry::remove_account_directory(const std::string &uuid) {
  const auto path = checked_path(accounts_root_, uuid);
  if (!account_directory_exists(uuid)) {
    return;
  }
  for (const auto &entry : std::filesystem::recursive_directory_iterator(path)) {
    if (std::filesystem::is_symlink(entry.symlink_status())) {
      throw std::runtime_error("storage.unsafe_path");
    }
  }
  std::filesystem::remove_all(path);
  sync_directory(accounts_root_);
}
} // namespace telebezel
