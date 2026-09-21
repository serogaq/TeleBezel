#pragma once
#include "telebezel/runtime/account_state.hpp"
#include "telebezel/status.hpp"
#include <condition_variable>
#include <mutex>
#include <set>
#include <unordered_map>
namespace telebezel::runtime {
// Account entries have stable addresses. The receive loop and operations lock
// this store while reading/mutating account state, never while awaiting TDLib.
struct AccountStore {
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  StatusSnapshot status_;
  std::map<std::string, AccountState> accounts_;
  std::unordered_map<std::int32_t, std::string> client_accounts_;
  std::set<std::string> orphan_accounts_;
  std::map<std::string, std::string> manifest_errors_;
};
} // namespace telebezel::runtime
