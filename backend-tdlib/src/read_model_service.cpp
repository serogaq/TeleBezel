#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
nlohmann::json ReadModelService::chats(const std::string &uuid, const std::string &list, std::size_t limit,
                                       const std::string &cursor) {
  std::int32_t client = 0;
  std::uint64_t lower_sequence = 0;
  std::uint64_t authorization_generation = 1;
  std::string generation;
  std::optional<std::pair<std::int64_t, std::int64_t>> boundary;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !found->second.reconciled || found->second.authorization_state != "ready")
      return safe_error("service.busy", 503);
    client = found->second.client_id;
    lower_sequence = found->second.event_sequence;
    authorization_generation = found->second.authorization_generation;
    generation = found->second.generation;
  }
  if (!cursor.empty()) {
    const auto dot = cursor.rfind('.');
    const auto payload = dot == std::string::npos ? std::string{} : cursor.substr(0, dot);
    const auto signature = sha256_hex(config_.internal_token + ":" + uuid + ":" + generation + ":" + payload);
    std::vector<std::string> fields;
    std::size_t position = 0;
    while (position <= payload.size()) {
      const auto separator = payload.find(':', position);
      fields.push_back(
          payload.substr(position, separator == std::string::npos ? std::string::npos : separator - position));
      if (separator == std::string::npos)
        break;
      position = separator + 1;
    }
    try {
      const auto now =
          static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
      if (dot == std::string::npos || !token_matches(signature, cursor.substr(dot + 1)) || fields.size() != 6 ||
          fields[0] != "l" || std::stoull(fields[1]) != authorization_generation || fields[2] != list ||
          std::stoull(fields[3]) < now)
        return safe_error("cursor.unusable", 409);
      boundary = {std::stoll(fields[4]), std::stoll(fields[5])};
    } catch (const std::exception &) {
      return safe_error("cursor.unusable", 409);
    }
  }
  bool load_failed = false;
  td_api::object_ptr<td_api::ChatList> chat_list =
      list == "archive" ? td_api::object_ptr<td_api::ChatList>(td_api::make_object<td_api::chatListArchive>())
                        : td_api::object_ptr<td_api::ChatList>(td_api::make_object<td_api::chatListMain>());
  const auto response =
      broker_.request(client, td_api::make_object<td_api::getChats>(std::move(chat_list), 50), std::chrono::seconds(6));
  load_failed = !response || response->get_id() == td_api::error::ID;
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end() || found->second.tombstone)
    return safe_error("account.not_found", 404);
  const auto &account = found->second;
  if (!account.reconciled || account.authorization_state != "ready")
    return safe_error("service.busy", 503);
  std::vector<nlohmann::json> items;
  for (const auto &[chat_id, projection] : account.chats) {
    static_cast<void>(chat_id);
    if (projection["positions"].contains(list))
      items.push_back(projection);
  }
  std::sort(items.begin(), items.end(), [&list](const auto &left, const auto &right) {
    const auto left_order = std::stoll(left["positions"][list].value("order", "0"));
    const auto right_order = std::stoll(right["positions"][list].value("order", "0"));
    return left_order == right_order ? std::stoll(left.value("id", "0")) > std::stoll(right.value("id", "0"))
                                     : left_order > right_order;
  });
  if (boundary) {
    items.erase(items.begin(), std::find_if(items.begin(), items.end(), [&list, &boundary](const auto &item) {
                  const auto order = std::stoll(item["positions"][list].value("order", "0"));
                  const auto id = std::stoll(item.value("id", "0"));
                  return order < boundary->first || (order == boundary->first && id < boundary->second);
                }));
  }
  const bool truncated = items.size() > limit;
  nlohmann::json next_cursor = nullptr;
  if (truncated) {
    const auto &last = items[limit - 1];
    const auto expires = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + 900;
    const auto payload = "l:" + std::to_string(account.authorization_generation) + ":" + list + ":" +
                         std::to_string(expires) + ":" + last["positions"][list].value("order", "0") + ":" +
                         last.value("id", "0");
    next_cursor = payload + "." +
                  sha256_hex(config_.internal_token + ":" + account.uuid + ":" + account.generation + ":" + payload);
  }
  if (truncated)
    items.resize(limit);
  return {{"items", items},
          {"stale", true},
          {"partial", load_failed},
          {"has_more", load_failed ? nlohmann::json(nullptr) : nlohmann::json(truncated)},
          {"next_cursor", next_cursor},
          {"retry_cursor",
           load_failed ? (cursor.empty() ? nlohmann::json(nullptr) : nlohmann::json(cursor)) : nlohmann::json(nullptr)},
          {"updates_cursor", cursors_.encode(account, lower_sequence)},
          {"observed_at", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
          {"source", load_failed ? "tdlib_memory" : "tdlib"}};
}

nlohmann::json ReadModelService::chat(const std::string &uuid, std::int64_t chat_id) const {
  std::lock_guard lock(context_.mutex_);
  const auto account = context_.accounts_.find(uuid);
  if (account == context_.accounts_.end() || !account->second.reconciled)
    return safe_error("account.not_found", 404);
  const auto found = account->second.chats.find(chat_id);
  if (found == account->second.chats.end())
    return safe_error("chat.not_found", 404);
  return {{"item", found->second},
          {"stale", true},
          {"partial", false},
          {"updates_cursor", cursors_.encode(account->second, account->second.event_sequence)},
          {"source", "tdlib_memory"}};
}

nlohmann::json ReadModelService::messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit,
                                          const std::string &cursor) {
  std::int32_t client = 0;
  std::uint64_t lower_sequence = 0;
  std::uint64_t authorization_generation = 1;
  std::string epoch;
  std::string generation;
  std::int64_t anchor = 0;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !account->second.reconciled || account->second.client_id == 0)
      return safe_error("account.not_found", 404);
    client = account->second.client_id;
    lower_sequence = account->second.event_sequence;
    epoch = account->second.runtime_epoch;
    generation = account->second.generation;
    authorization_generation = account->second.authorization_generation;
  }
  if (!cursor.empty()) {
    const auto dot = cursor.rfind('.');
    const std::string prefix = "h:" + std::to_string(authorization_generation) + ":" + std::to_string(chat_id) + ":";
    if (dot == std::string::npos || !cursor.starts_with(prefix))
      return safe_error("cursor.unusable", 409);
    const auto payload = cursor.substr(0, dot);
    const auto signature = sha256_hex(config_.internal_token + ":" + uuid + ":" + generation + ":" + payload);
    if (!token_matches(signature, cursor.substr(dot + 1)))
      return safe_error("cursor.unusable", 409);
    try {
      anchor = std::stoll(payload.substr(prefix.size()));
    } catch (const std::exception &) {
      return safe_error("cursor.unusable", 409);
    }
  }
  bool fallback = false;
  auto response = broker_.request(
      client, td_api::make_object<td_api::getChatHistory>(chat_id, anchor, 0, static_cast<std::int32_t>(limit), false),
      std::chrono::seconds(6));
  if (!response || response->get_id() == td_api::error::ID) {
    fallback = true;
    response = broker_.request(
        client, td_api::make_object<td_api::getChatHistory>(chat_id, anchor, 0, static_cast<std::int32_t>(limit), true),
        std::chrono::seconds(2));
  }
  std::vector<nlohmann::json> items;
  if (response && response->get_id() == td_api::messages::ID) {
    auto &history = static_cast<td_api::messages &>(*response);
    for (const auto &item : history.messages_)
      if (item && (anchor == 0 || item->id_ != anchor))
        items.push_back(message_projection(*item));
  }
  std::lock_guard lock(context_.mutex_);
  const auto account = context_.accounts_.find(uuid);
  if (account == context_.accounts_.end() || account->second.runtime_epoch != epoch)
    return safe_error("sync.resync_required", 409);
  if (!account->second.events.empty() && lower_sequence + 1 < account->second.events.front().value("sequence", 0ULL))
    return safe_error("sync.resync_required", 409);
  for (const auto &item : items) {
    const auto message_id = std::stoll(item.value("id", "0"));
    const auto changed_after_read = std::any_of(account->second.events.begin(), account->second.events.end(),
                                                [lower_sequence, chat_id, message_id](const auto &event) {
                                                  return event.value("sequence", 0ULL) > lower_sequence &&
                                                         event.value("chat_id", "") == std::to_string(chat_id) &&
                                                         event.value("message_id", "") == std::to_string(message_id);
                                                });
    if (!changed_after_read)
      account->second.messages[{chat_id, message_id}] = item;
  }
  if (items.empty()) {
    for (auto iterator = account->second.messages.rbegin(); iterator != account->second.messages.rend(); ++iterator) {
      if (iterator->first.first == chat_id && (anchor == 0 || iterator->first.second < anchor))
        items.push_back(iterator->second);
      if (items.size() == limit)
        break;
    }
    fallback = true;
  }
  nlohmann::json next_cursor = nullptr;
  if (!items.empty()) {
    const auto payload = "h:" + std::to_string(account->second.authorization_generation) + ":" +
                         std::to_string(chat_id) + ":" + items.back().value("id", "0");
    next_cursor = payload + "." + sha256_hex(config_.internal_token + ":" + uuid + ":" + generation + ":" + payload);
  }
  return {{"items", items},
          {"stale", true},
          {"partial", fallback || items.size() < limit},
          {"has_more", nullptr},
          {"next_cursor", next_cursor},
          {"retry_cursor", (fallback || items.size() < limit)
                               ? (cursor.empty() ? nlohmann::json(next_cursor) : nlohmann::json(cursor))
                               : nlohmann::json(nullptr)},
          {"updates_cursor", cursors_.encode(account->second, lower_sequence)},
          {"observed_at", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
          {"source", fallback ? "tdlib_local" : "tdlib"}};
}

nlohmann::json ReadModelService::message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id) {
  std::int32_t client = 0;
  std::uint64_t lower_sequence = 0;
  std::string epoch;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !account->second.reconciled)
      return safe_error("account.not_found", 404);
    const auto found = account->second.messages.find({chat_id, message_id});
    if (found != account->second.messages.end())
      return {{"item", found->second},
              {"stale", true},
              {"partial", false},
              {"updates_cursor", cursors_.encode(account->second, account->second.event_sequence)},
              {"source", "tdlib_memory"}};
    client = account->second.client_id;
    lower_sequence = account->second.event_sequence;
    epoch = account->second.runtime_epoch;
  }
  const auto response =
      broker_.request(client, td_api::make_object<td_api::getMessage>(chat_id, message_id), std::chrono::seconds(6));
  if (!response || response->get_id() != td_api::message::ID)
    return safe_error("message.not_found", 404);
  const auto projection = message_projection(static_cast<const td_api::message &>(*response));
  std::lock_guard lock(context_.mutex_);
  auto &account = context_.accounts_.at(uuid);
  if (account.runtime_epoch != epoch)
    return safe_error("sync.resync_required", 409);
  const auto changed_after_read = std::any_of(account.events.begin(), account.events.end(),
                                              [lower_sequence, chat_id, message_id](const auto &event) {
                                                return event.value("sequence", 0ULL) > lower_sequence &&
                                                       event.value("chat_id", "") == std::to_string(chat_id) &&
                                                       event.value("message_id", "") == std::to_string(message_id);
                                              });
  if (changed_after_read) {
    if (const auto current = account.messages.find({chat_id, message_id}); current != account.messages.end())
      return {{"item", current->second},
              {"stale", true},
              {"partial", false},
              {"updates_cursor", cursors_.encode(account, lower_sequence)},
              {"source", "tdlib_memory"}};
    return safe_error("sync.resync_required", 409);
  }
  account.messages[{chat_id, message_id}] = projection;
  return {{"item", projection},
          {"stale", true},
          {"partial", false},
          {"updates_cursor", cursors_.encode(account, account.event_sequence)},
          {"source", "tdlib"}};
}

nlohmann::json ReadModelService::updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const {
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end() || !found->second.reconciled)
    return safe_error("account.not_found", 404);
  const auto &account = found->second;
  std::uint64_t sequence = account.event_sequence;
  if (!cursor.empty()) {
    const auto parsed = cursors_.decode(account, cursor);
    if (!parsed)
      return safe_error("sync.resync_required", 409);
    sequence = *parsed;
  }
  if (!account.events.empty() && sequence + 1 < account.events.front().value("sequence", 0ULL))
    return safe_error("sync.resync_required", 409);
  std::vector<nlohmann::json> items;
  auto last = sequence;
  for (const auto &event : account.events) {
    if (event.value("sequence", 0ULL) <= sequence)
      continue;
    items.push_back(event);
    last = event.value("sequence", last);
    if (items.size() == limit)
      break;
  }
  return {{"items", items}, {"cursor", cursors_.encode(account, last)}, {"has_more", last < account.event_sequence}};
}

} // namespace telebezel::runtime
