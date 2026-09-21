#pragma once
#include "telebezel/config.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/status.hpp"
#include "telebezel/transport.hpp"
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace telebezel {
class TdRuntime final {
public:
  TdRuntime(const Config &config, AccountRegistry &registry, std::unique_ptr<TdTransport> transport = {});
  ~TdRuntime();
  TdRuntime(const TdRuntime &) = delete;
  TdRuntime &operator=(const TdRuntime &) = delete;
  void start();
  void stop();
  StatusSnapshot status() const;
  nlohmann::json snapshots(const std::vector<std::string> &ids) const;
  nlohmann::json snapshot(const std::string &uuid) const;
  nlohmann::json reconcile(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json authorization_action(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json logout(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json update_proxy(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json ping_proxy(const std::string &uuid, const nlohmann::json &proxy);
  nlohmann::json remove(const std::string &uuid, const nlohmann::json &command);
  nlohmann::json chats(const std::string &uuid, const std::string &list, std::size_t limit, const std::string &cursor);
  nlohmann::json chat(const std::string &uuid, std::int64_t chat_id) const;
  nlohmann::json messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit, const std::string &cursor);
  nlohmann::json message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id);
  nlohmann::json preview(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id,
                         const std::string &preview_id);
  nlohmann::json updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const;
  nlohmann::json set_interest(const std::string &uuid, std::int64_t chat_id, const std::string &lease_key, bool active,
                              bool await_transition = true);
  nlohmann::json release_interests(const std::string &principal_type, const std::string &principal_id);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace telebezel
