#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
nlohmann::json InterestLeaseManager::set_interest(const std::string &uuid, std::int64_t chat_id,
                                                  const std::string &lease_key, bool active) {
  std::int32_t client = 0;
  bool transition = false;
  std::optional<std::pair<std::int64_t, std::chrono::steady_clock::time_point>> removed_lease;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !found->second.reconciled || found->second.client_id == 0)
      return safe_error("account.not_found", 404);
    auto &account = found->second;
    const auto now = std::chrono::steady_clock::now();
    for (auto iterator = account.interests.begin(); iterator != account.interests.end();) {
      if (iterator->second.second > now) {
        ++iterator;
        continue;
      }
      auto &count = account.interest_counts[iterator->second.first];
      if (count > 0)
        --count;
      iterator = account.interests.erase(iterator);
    }
    const auto existing = account.interests.find(lease_key);
    if (active) {
      if (existing == account.interests.end()) {
        if (account.interests.size() >= 20)
          return safe_error("interest.limit_reached", 429);
        const auto first_separator = lease_key.find(':');
        const auto second_separator =
            first_separator == std::string::npos ? std::string::npos : lease_key.find(':', first_separator + 1);
        const auto principal_prefix =
            second_separator == std::string::npos ? lease_key : lease_key.substr(0, second_separator + 1);
        const auto principal_count =
            std::count_if(account.interests.begin(), account.interests.end(),
                          [&principal_prefix](const auto &entry) { return entry.first.starts_with(principal_prefix); });
        if (principal_count >= 4)
          return safe_error("interest.limit_reached", 429);
        transition = account.interest_counts[chat_id]++ == 0;
      }
      account.interests[lease_key] = {chat_id, now + std::chrono::seconds(90)};
    } else if (existing != account.interests.end()) {
      removed_lease = existing->second;
      auto &count = account.interest_counts[existing->second.first];
      if (count > 0) {
        --count;
        transition = count == 0;
      }
      account.interests.erase(existing);
    }
    client = account.client_id;
  }
  if (transition) {
    const auto response = active ? broker_.request(client, td_api::make_object<td_api::openChat>(chat_id))
                                 : broker_.request(client, td_api::make_object<td_api::closeChat>(chat_id));
    if (!response || response->get_id() == td_api::error::ID) {
      if (active) {
        std::lock_guard lock(context_.mutex_);
        if (auto account = context_.accounts_.find(uuid); account != context_.accounts_.end()) {
          if (auto lease = account->second.interests.find(lease_key); lease != account->second.interests.end()) {
            auto &count = account->second.interest_counts[lease->second.first];
            if (count > 0)
              --count;
            account->second.interests.erase(lease);
          }
        }
      }
      if (!active && removed_lease) {
        // Keep a failed withdrawal retryable. Restoring its original expiry also
        // lets the receive loop close it if the caller does not retry.
        std::lock_guard lock(context_.mutex_);
        if (auto account = context_.accounts_.find(uuid); account != context_.accounts_.end() &&
                                                          account->second.client_id == client &&
                                                          !account->second.interests.contains(lease_key)) {
          account->second.interests.emplace(lease_key, *removed_lease);
          ++account->second.interest_counts[removed_lease->first];
        }
      }
      return safe_error("telegram.operation_failed", 502);
    }
    bool compensate = false;
    {
      std::lock_guard lock(context_.mutex_);
      if (const auto account = context_.accounts_.find(uuid); account != context_.accounts_.end()) {
        const auto count = account->second.interest_counts.find(chat_id);
        const bool wanted_open = count != account->second.interest_counts.end() && count->second > 0;
        compensate = wanted_open != active;
      }
    }
    if (compensate) {
      if (active)
        transport_.send(client, broker_.next_id(), td_api::make_object<td_api::closeChat>(chat_id));
      else
        transport_.send(client, broker_.next_id(), td_api::make_object<td_api::openChat>(chat_id));
    }
  }
  return {{"active", active}, {"expires_in", active ? 90 : 0}};
}

nlohmann::json InterestLeaseManager::release_interests(const std::string &principal_type,
                                                       const std::string &principal_id) {
  const auto prefix = principal_type + ":" + principal_id + ":";
  std::vector<std::pair<std::int32_t, std::int64_t>> closes;
  std::size_t released = 0;
  {
    std::lock_guard lock(context_.mutex_);
    for (auto &[uuid, account] : context_.accounts_) {
      static_cast<void>(uuid);
      for (auto iterator = account.interests.begin(); iterator != account.interests.end();) {
        if (!iterator->first.starts_with(prefix)) {
          ++iterator;
          continue;
        }
        const auto chat_id = iterator->second.first;
        auto &count = account.interest_counts[chat_id];
        if (count > 0) {
          --count;
          if (count == 0 && account.client_id != 0)
            closes.emplace_back(account.client_id, chat_id);
        }
        iterator = account.interests.erase(iterator);
        ++released;
      }
    }
  }
  for (const auto &[client, chat_id] : closes)
    transport_.send(client, broker_.next_id(), td_api::make_object<td_api::closeChat>(chat_id));
  return {{"released", released}};
}

} // namespace telebezel::runtime
