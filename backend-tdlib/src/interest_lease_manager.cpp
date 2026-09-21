#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include <algorithm>
#include <chrono>
#include <td/telegram/td_api.h>

namespace telebezel::runtime {
namespace {
using Phase = InterestChatState::Phase;
struct Transition {
  std::string uuid;
  std::string epoch;
  std::int32_t client;
  std::int64_t chat_id;
  std::uint64_t number;
  bool opening;
};

void expire_leases(AccountState &account, std::chrono::steady_clock::time_point now) {
  for (auto item = account.interests.begin(); item != account.interests.end();) {
    if (item->second.second > now) {
      ++item;
      continue;
    }
    auto &count = account.interest_counts[item->second.first];
    if (count > 0)
      --count;
    item = account.interests.erase(item);
  }
}
} // namespace

InterestLeaseManager::~InterestLeaseManager() {
  stopping_.store(true);
  context_.condition_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

nlohmann::json InterestLeaseManager::set_interest(const std::string &uuid, std::int64_t chat_id,
                                                  const std::string &lease_key, bool active, bool await_transition) {
  std::unique_lock lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end() || !found->second.reconciled || found->second.lifecycle != "active" ||
      found->second.client_id == 0)
    return safe_error("account.not_found", 404);
  auto &account = found->second;
  const auto now = std::chrono::steady_clock::now();
  expire_leases(account, now);
  auto existing = account.interests.find(lease_key);
  if (active) {
    if (existing == account.interests.end()) {
      if (account.interests.size() >= 20)
        return safe_error("interest.limit_reached", 429);
      const auto first = lease_key.find(':');
      const auto second = first == std::string::npos ? std::string::npos : lease_key.find(':', first + 1);
      const auto principal = second == std::string::npos ? lease_key : lease_key.substr(0, second + 1);
      if (std::count_if(account.interests.begin(), account.interests.end(),
                        [&principal](const auto &entry) { return entry.first.starts_with(principal); }) >= 4)
        return safe_error("interest.limit_reached", 429);
      ++account.interest_counts[chat_id];
    }
    account.interests[lease_key] = {chat_id, now + std::chrono::seconds(90)};
  } else if (existing != account.interests.end()) {
    auto &count = account.interest_counts[existing->second.first];
    if (count > 0)
      --count;
    account.interests.erase(existing);
  }
  const auto failed_before = account.interest_states[chat_id].failed_transition;
  const auto client = account.client_id;
  const auto epoch = account.runtime_epoch;
  context_.condition_.notify_all();
  if (!await_transition)
    return {{"active", active}, {"expires_in", active ? 90 : 0}};
  const auto deadline = now + std::chrono::seconds(8);
  while (true) {
    if (account.client_id != client || account.runtime_epoch != epoch || account.lifecycle != "active" ||
        account.closed || account.tombstone)
      return safe_error("authorization.invalid_state", 409);
    const auto state = account.interest_states.find(chat_id);
    if (state == account.interest_states.end())
      return safe_error("authorization.invalid_state", 409);
    const auto desired = account.interest_counts[chat_id] > 0;
    if (active && !account.interests.contains(lease_key))
      return safe_error("telegram.operation_failed", 502);
    if (active && state->second.phase == Phase::Open)
      return {{"active", true}, {"expires_in", 90}};
    if (!active && (desired || state->second.phase == Phase::Closed))
      return {{"active", false}, {"expires_in", 0}};
    if (state->second.failed_transition > failed_before)
      return safe_error("telegram.operation_failed", 502);
    if (context_.condition_.wait_until(lock, deadline) == std::cv_status::timeout)
      return safe_error("service.busy", 503);
  }
}

nlohmann::json InterestLeaseManager::release_interests(const std::string &principal_type,
                                                       const std::string &principal_id) {
  const auto prefix = principal_type + ":" + principal_id + ":";
  std::size_t released = 0;
  {
    std::lock_guard lock(context_.mutex_);
    for (auto &[uuid, account] : context_.accounts_) {
      static_cast<void>(uuid);
      for (auto item = account.interests.begin(); item != account.interests.end();) {
        if (!item->first.starts_with(prefix)) {
          ++item;
          continue;
        }
        auto &count = account.interest_counts[item->second.first];
        if (count > 0)
          --count;
        item = account.interests.erase(item);
        ++released;
      }
    }
  }
  context_.condition_.notify_all();
  return {{"released", released}};
}

void InterestLeaseManager::run() {
  while (!stopping_.load()) {
    std::optional<Transition> selected;
    {
      std::unique_lock lock(context_.mutex_);
      const auto now = std::chrono::steady_clock::now();
      for (auto &[uuid, account] : context_.accounts_) {
        for (const auto &[chat_id, count] : account.interest_counts)
          if (count > 0 && !account.interest_states.contains(chat_id))
            account.interest_states[chat_id].phase = Phase::Open;
        expire_leases(account, now);
        if (!account.reconciled || account.lifecycle != "active" || account.client_id == 0 || account.closed ||
            account.tombstone)
          continue;
        for (auto &[chat_id, state] : account.interest_states) {
          const bool desired = account.interest_counts[chat_id] > 0;
          const bool opening = desired && state.phase == Phase::Closed;
          const bool closing = !desired && state.phase == Phase::Open;
          if ((!opening && !closing) || state.retry_at > now)
            continue;
          state.phase = opening ? Phase::Opening : Phase::Closing;
          selected = Transition{uuid, account.runtime_epoch, account.client_id, chat_id, ++state.transition, opening};
          break;
        }
        if (selected)
          break;
      }
      if (!selected) {
        context_.condition_.wait_for(lock, std::chrono::milliseconds(100));
        continue;
      }
    }
    bool succeeded = false;
    try {
      auto response = selected->opening
                          ? broker_.request(selected->client, td_api::make_object<td_api::openChat>(selected->chat_id),
                                            std::chrono::seconds(3))
                          : broker_.request(selected->client, td_api::make_object<td_api::closeChat>(selected->chat_id),
                                            std::chrono::seconds(3));
      succeeded = response && response->get_id() != td_api::error::ID;
    } catch (const std::exception &) {
      succeeded = false;
    }
    {
      std::lock_guard lock(context_.mutex_);
      const auto found = context_.accounts_.find(selected->uuid);
      if (found != context_.accounts_.end() && found->second.lifecycle == "active" &&
          found->second.client_id == selected->client && found->second.runtime_epoch == selected->epoch) {
        auto &account = found->second;
        auto &state = account.interest_states[selected->chat_id];
        if (state.transition == selected->number) {
          state.phase = succeeded ? (selected->opening ? Phase::Open : Phase::Closed)
                                  : (selected->opening ? Phase::Closed : Phase::Open);
          if (!succeeded) {
            state.failed_transition = selected->number;
            state.retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            if (selected->opening) {
              for (auto item = account.interests.begin(); item != account.interests.end();) {
                if (item->second.first == selected->chat_id)
                  item = account.interests.erase(item);
                else
                  ++item;
              }
              account.interest_counts[selected->chat_id] = 0;
            }
          }
        }
      }
    }
    context_.condition_.notify_all();
  }
}
} // namespace telebezel::runtime
