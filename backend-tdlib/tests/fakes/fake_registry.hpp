#pragma once
#include "telebezel/registry.hpp"
#include <mutex>
#include <set>
#include <stdexcept>

namespace telebezel::testing {
// In-memory durable boundary. Write failures occur before mutation, just as a
// failed atomic manifest replacement must leave the previous manifest intact.
class FakeRegistry final : public AccountRegistry {
public:
  explicit FakeRegistry(std::filesystem::path root) : root_(std::move(root)) {}
  const std::filesystem::path &root() const override { return root_; }
  void open() override {}
  std::vector<AccountManifest> discover() const override {
    std::lock_guard lock(mutex_);
    std::vector<AccountManifest> result;
    for (const auto &[id, manifest] : manifests_)
      result.push_back(manifest);
    return result;
  }
  std::vector<std::string> orphan_account_ids() const override {
    std::lock_guard lock(mutex_);
    std::vector<std::string> result;
    for (const auto &id : directories_)
      if (!manifests_.contains(id))
        result.push_back(id);
    return result;
  }
  std::map<std::string, std::string> manifest_errors() const override { return {}; }
  std::optional<AccountManifest> read(const std::string &uuid) const override {
    std::lock_guard lock(mutex_);
    auto found = manifests_.find(uuid);
    return found == manifests_.end() ? std::nullopt : std::optional(found->second);
  }
  void write(const AccountManifest &manifest, const nlohmann::json &proxy) override {
    std::lock_guard lock(mutex_);
    ++writes_;
    if (fail_write_at_ == writes_)
      throw std::runtime_error("storage.write_failed");
    auto persisted = manifest;
    persisted.proxy = proxy;
    manifests_[manifest.uuid] = std::move(persisted);
  }
  void persist_authorization_generation(const std::string &uuid, std::uint64_t generation) override {
    std::lock_guard lock(mutex_);
    auto found = manifests_.find(uuid);
    if (found != manifests_.end() && !found->second.tombstone && found->second.authorization_generation < generation)
      found->second.authorization_generation = generation;
  }
  void ensure_account_directories(const std::string &uuid) override {
    std::lock_guard lock(mutex_);
    directories_.insert(uuid);
  }
  bool account_directory_exists(const std::string &uuid) const override {
    std::lock_guard lock(mutex_);
    return directories_.contains(uuid);
  }
  void remove_account_directory(const std::string &uuid) override {
    std::lock_guard lock(mutex_);
    directories_.erase(uuid);
  }
  void fail_write_after(std::size_t count) {
    std::lock_guard lock(mutex_);
    fail_write_at_ = writes_ + count;
  }

private:
  std::filesystem::path root_;
  mutable std::mutex mutex_;
  std::map<std::string, AccountManifest> manifests_;
  std::set<std::string> directories_;
  std::size_t writes_{0};
  std::size_t fail_write_at_{0};
};
} // namespace telebezel::testing
