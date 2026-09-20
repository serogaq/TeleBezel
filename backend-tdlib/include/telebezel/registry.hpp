#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace telebezel {
struct AccountManifest {
  std::uint32_t format_version{1};
  std::string uuid;
  std::string generation;
  bool use_test_dc{false};
  std::uint32_t key_derivation_version{1};
  std::uint64_t revision{0};
  std::string lifecycle{"provisioning"};
  std::string operation_id;
  bool tombstone{false};
  nlohmann::json proxy{nlohmann::json::object()};
  std::string content_hash;
};

class Registry final {
public:
  explicit Registry(std::filesystem::path root);
  ~Registry();
  Registry(const Registry &) = delete;
  Registry &operator=(const Registry &) = delete;
  void open();
  std::vector<AccountManifest> discover() const;
  std::vector<std::string> orphan_account_ids() const;
  std::map<std::string, std::string> manifest_errors() const;
  std::optional<AccountManifest> read(const std::string &uuid) const;
  void write(const AccountManifest &manifest, const nlohmann::json &proxy);
  void ensure_account_directories(const std::string &uuid);
  bool account_directory_exists(const std::string &uuid) const;
  void remove_account_directory(const std::string &uuid);
  const std::filesystem::path &root() const { return root_; }

private:
  std::filesystem::path checked_path(const std::filesystem::path &base, const std::string &component) const;
  std::filesystem::path root_;
  std::filesystem::path registry_root_;
  std::filesystem::path accounts_root_;
  int lock_fd_{-1};
};
bool valid_uuid(const std::string &value);
} // namespace telebezel
