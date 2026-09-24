#pragma once
#include "telebezel/config.hpp"
#include "telebezel/runtime/account_state.hpp"
#include "telebezel/status.hpp"
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>
namespace telebezel::runtime {
// Account entries have stable addresses. The receive loop and operations lock
// this store while reading/mutating account state, never while awaiting TDLib.
struct AccountStore {
  explicit AccountStore(const Config &config) : limits_(config) {}

  using MessageKey = AccountState::MessageKey;

  void erase_message(AccountState &account, const MessageKey &key) {
    const auto found = account.messages.find(key);
    if (found == account.messages.end())
      return;
    const auto meta = account.message_meta.find(key);
    const auto bytes = meta == account.message_meta.end() ? found->second.dump().size() : meta->second.bytes;
    const auto date =
        meta == account.message_meta.end() ? found->second.value("date", std::int64_t{0}) : meta->second.date;
    account.message_age.erase({date, key.first, key.second});
    age_index_.erase({date, &account, key.first, key.second});
    if (meta != account.message_meta.end())
      account.message_meta.erase(meta);
    if (const auto chat = account.chat_message_counts.find(key.first); chat != account.chat_message_counts.end()) {
      if (--chat->second == 0)
        account.chat_message_counts.erase(chat);
    }
    account.message_bytes -= bytes;
    total_bytes_ -= bytes;
    --total_messages_;
    account.messages.erase(found);
  }

  void clear_messages(AccountState &account) {
    for (const auto &[date, chat, message] : account.message_age)
      age_index_.erase({date, &account, chat, message});
    total_messages_ -= account.messages.size();
    total_bytes_ -= account.message_bytes;
    account.messages.clear();
    account.message_meta.clear();
    account.chat_message_counts.clear();
    account.message_age.clear();
    account.message_bytes = 0;
  }

  void put_message(AccountState &account, const MessageKey &key, nlohmann::json projection) {
    erase_message(account, key);
    const CachedMessageMeta meta{projection.dump().size(), projection.value("date", std::int64_t{0})};
    account.messages.emplace(key, std::move(projection));
    account.message_meta.emplace(key, meta);
    account.message_age.emplace(meta.date, key.first, key.second);
    age_index_.emplace(meta.date, &account, key.first, key.second);
    ++account.chat_message_counts[key.first];
    account.message_bytes += meta.bytes;
    total_bytes_ += meta.bytes;
    ++total_messages_;
    while (account.chat_message_counts[key.first] > limits_.cache_messages_per_chat) {
      const auto oldest = account.messages.lower_bound({key.first, std::numeric_limits<std::int64_t>::min()});
      if (oldest == account.messages.end() || oldest->first.first != key.first)
        break;
      erase_message(account, oldest->first);
    }
    while (account.messages.size() > limits_.cache_messages_per_account && evict_one(&account)) {
    }
    enforce_process_budget();
  }

  void put_sender_name(AccountState &account, const std::string &key, const std::string &name) {
    const auto found = account.sender_names.find(key);
    if (found != account.sender_names.end()) {
      account.sender_bytes -= key.size() + found->second.size();
      total_bytes_ -= key.size() + found->second.size();
      found->second = name;
    } else {
      account.sender_names.emplace(key, name);
      account.sender_order.push_back(key);
    }
    account.sender_bytes += key.size() + name.size();
    total_bytes_ += key.size() + name.size();
    while (account.sender_names.size() > limits_.cache_messages_per_account && !account.sender_order.empty()) {
      const auto victim = account.sender_names.find(account.sender_order.front());
      account.sender_order.pop_front();
      if (victim == account.sender_names.end())
        continue;
      account.sender_bytes -= victim->first.size() + victim->second.size();
      total_bytes_ -= victim->first.size() + victim->second.size();
      account.sender_names.erase(victim);
    }
    enforce_process_budget();
  }

  void clear_sender_names(AccountState &account) {
    total_bytes_ -= account.sender_bytes;
    account.sender_bytes = 0;
    account.sender_names.clear();
    account.sender_order.clear();
  }

  void reset_session(AccountState &account) {
    account.chats.clear();
    clear_messages(account);
    clear_sender_names(account);
    account.events.clear();
    account.send_contexts.clear();
    account.member_statuses.clear();
    account.deleted_users.clear();
    account.sends.clear();
    account.send_messages.clear();
    account.sending_ids.clear();
    account.interests.clear();
    account.interest_counts.clear();
    account.interest_states.clear();
    account.main_exhausted = false;
    account.archive_exhausted = false;
    account.main_load_error.clear();
    account.archive_load_error.clear();
    account.main_orders.reset();
    account.archive_orders.reset();
    account.main_unread = {};
    account.archive_unread = {};
  }

  std::size_t cached_messages() const { return total_messages_; }
  std::size_t cached_bytes() const { return total_bytes_; }

  bool consistent() const {
    std::size_t messages = 0;
    std::size_t bytes = 0;
    std::size_t aged = 0;
    for (const auto &[uuid, account] : accounts_) {
      static_cast<void>(uuid);
      std::size_t account_bytes = 0;
      std::map<std::int64_t, std::size_t> per_chat;
      if (account.message_meta.size() != account.messages.size() ||
          account.message_age.size() != account.messages.size())
        return false;
      for (const auto &[key, projection] : account.messages) {
        const auto meta = account.message_meta.find(key);
        if (meta == account.message_meta.end() || meta->second.bytes != projection.dump().size() ||
            !account.message_age.contains({meta->second.date, key.first, key.second}) ||
            !age_index_.contains({meta->second.date, &account, key.first, key.second}))
          return false;
        account_bytes += meta->second.bytes;
        ++per_chat[key.first];
      }
      std::size_t names = 0;
      for (const auto &[key, name] : account.sender_names)
        names += key.size() + name.size();
      if (account_bytes != account.message_bytes || per_chat != account.chat_message_counts ||
          names != account.sender_bytes)
        return false;
      messages += account.messages.size();
      bytes += account_bytes + names;
      aged += account.message_age.size();
    }
    return messages == total_messages_ && bytes == total_bytes_ && aged == age_index_.size();
  }

private:
  using AgeKey = std::tuple<std::int64_t, const AccountState *, std::int64_t, std::int64_t>;
  static constexpr std::size_t eviction_scan = 4096;

  static bool active_chat(const AccountState &account, std::int64_t chat_id) {
    const auto found = account.interest_counts.find(chat_id);
    return found != account.interest_counts.end() && found->second > 0;
  }

  void enforce_process_budget() {
    while ((total_messages_ > limits_.cache_messages_per_process || total_bytes_ > limits_.cache_projection_bytes) &&
           evict_one(nullptr)) {
    }
  }

  bool evict_one(AccountState *scope) {
    if (scope != nullptr) {
      if (scope->message_age.empty())
        return false;
      auto victim = scope->message_age.begin();
      std::size_t scanned = 0;
      for (auto item = scope->message_age.begin(); item != scope->message_age.end() && scanned < eviction_scan;
           ++item, ++scanned) {
        if (!active_chat(*scope, std::get<1>(*item))) {
          victim = item;
          break;
        }
      }
      erase_message(*scope, {std::get<1>(*victim), std::get<2>(*victim)});
      return true;
    }
    if (age_index_.empty())
      return false;
    auto victim = age_index_.begin();
    std::size_t scanned = 0;
    for (auto item = age_index_.begin(); item != age_index_.end() && scanned < eviction_scan; ++item, ++scanned) {
      if (!active_chat(*std::get<1>(*item), std::get<2>(*item))) {
        victim = item;
        break;
      }
    }
    auto &account = const_cast<AccountState &>(*std::get<1>(*victim));
    erase_message(account, {std::get<2>(*victim), std::get<3>(*victim)});
    return true;
  }

  std::set<AgeKey> age_index_;
  std::size_t total_messages_{0};
  std::size_t total_bytes_{0};

public:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  StatusSnapshot status_;
  std::map<std::string, AccountState> accounts_;
  std::unordered_map<std::int32_t, std::string> client_accounts_;
  std::set<std::string> orphan_accounts_;
  std::map<std::string, std::string> manifest_errors_;
  const Config &limits_;
};
} // namespace telebezel::runtime
