#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
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
  std::string operation_phase;
  std::uint64_t applied_revision{0};
  std::uint64_t authorization_generation{1};
};

class AccountRegistry {
public:
  virtual ~AccountRegistry() = default;
  virtual const std::filesystem::path &root() const = 0;
  virtual void open() = 0;
  virtual std::vector<AccountManifest> discover() const = 0;
  virtual std::vector<std::string> orphan_account_ids() const = 0;
  virtual std::map<std::string, std::string> manifest_errors() const = 0;
  virtual std::optional<AccountManifest> read(const std::string &uuid) const = 0;
  virtual void write(const AccountManifest &manifest, const nlohmann::json &proxy) = 0;
  virtual void persist_authorization_generation(const std::string &uuid, std::uint64_t generation) = 0;
  virtual void ensure_account_directories(const std::string &uuid) = 0;
  virtual bool account_directory_exists(const std::string &uuid) const = 0;
  virtual void remove_account_directory(const std::string &uuid) = 0;
};

class Registry final : public AccountRegistry {
public:
  Registry(std::filesystem::path root, std::filesystem::path master_key_file);
  ~Registry() override;
  Registry(const Registry &) = delete;
  Registry &operator=(const Registry &) = delete;
  void open() override;
  std::vector<AccountManifest> discover() const override;
  std::vector<std::string> orphan_account_ids() const override;
  std::map<std::string, std::string> manifest_errors() const override;
  std::optional<AccountManifest> read(const std::string &uuid) const override;
  void write(const AccountManifest &manifest, const nlohmann::json &proxy) override;
  void persist_authorization_generation(const std::string &uuid, std::uint64_t generation) override;
  void ensure_account_directories(const std::string &uuid) override;
  bool account_directory_exists(const std::string &uuid) const override;
  void remove_account_directory(const std::string &uuid) override;
  const std::filesystem::path &root() const override { return root_; }

private:
  std::filesystem::path checked_path(const std::filesystem::path &base, const std::string &component) const;
  std::filesystem::path root_;
  std::filesystem::path registry_root_;
  std::filesystem::path accounts_root_;
  std::filesystem::path master_key_file_;
  int lock_fd_{-1};
  mutable std::recursive_mutex manifest_mutex_;
};
bool valid_uuid(const std::string &value);
} // namespace telebezel
