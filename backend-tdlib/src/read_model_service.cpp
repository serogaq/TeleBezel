#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <openssl/evp.h>
#include <td/telegram/td_api.h>
#include <unordered_set>
namespace telebezel::runtime {
namespace {
struct ReadFence {
  std::string generation;
  std::string epoch;
  std::int32_t client{0};
  std::uint64_t authorization_generation{0};
  std::uint64_t sequence{0};
};

std::optional<ReadFence> read_fence(const AccountState &account) {
  if (!account.reconciled || account.closed || account.tombstone || account.lifecycle != "active" ||
      account.authorization_state != "ready" || account.client_id == 0)
    return std::nullopt;
  return ReadFence{account.generation, account.runtime_epoch, account.client_id, account.authorization_generation,
                   account.event_sequence};
}

bool fence_valid(const AccountState &account, const ReadFence &fence) {
  if (!read_fence(account) || account.generation != fence.generation || account.runtime_epoch != fence.epoch ||
      account.client_id != fence.client || account.authorization_generation != fence.authorization_generation)
    return false;
  return account.events.empty() || fence.sequence + 1 >= account.events.front().value("sequence", 0ULL);
}

std::string preview_token(const Config &config, const std::string &uuid, const ReadFence &fence, std::int64_t chat_id,
                          std::int64_t message_id, std::int32_t file_id) {
  return sha256_hex(config.internal_token + ":preview:" + uuid + ":" + fence.generation + ":" + fence.epoch + ":" +
                    std::to_string(fence.authorization_generation) + ":" + std::to_string(chat_id) + ":" +
                    std::to_string(message_id) + ":" + std::to_string(file_id));
}

std::string base64_encode(const std::string &bytes) {
  std::string output(4 * ((bytes.size() + 2) / 3), '\0');
  const auto count =
      EVP_EncodeBlock(reinterpret_cast<unsigned char *>(output.data()),
                      reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<int>(bytes.size()));
  output.resize(static_cast<std::size_t>(count));
  return output;
}
} // namespace
ReadModelService::~ReadModelService() {
  {
    std::lock_guard lock(refresh_mutex_);
    refresh_stopping_ = true;
  }
  refresh_condition_.notify_all();
  if (refresh_thread_.joinable())
    refresh_thread_.join();
}

void ReadModelService::cancel_account(const std::string &uuid) {
  std::lock_guard lock(refresh_mutex_);
  for (auto item = refresh_queue_.begin(); item != refresh_queue_.end();) {
    if (item->uuid == uuid) {
      refresh_keys_.erase(item->key);
      item = refresh_queue_.erase(item);
    } else {
      ++item;
    }
  }
  for (auto item = preview_reservations_.begin(); item != preview_reservations_.end();) {
    if (item->first.starts_with(uuid + ":")) {
      preview_reserved_bytes_ -= item->second;
      item = preview_reservations_.erase(item);
    } else {
      ++item;
    }
  }
}

std::string ReadModelService::enqueue_refresh(RefreshJob job) {
  std::lock_guard lock(refresh_mutex_);
  if (refresh_stopping_)
    return "stopped";
  if (refresh_keys_.contains(job.key))
    return "pending";
  if (refresh_queue_.size() >= 128)
    return "saturated";
  if (job.preview_file_id != 0) {
    const auto account_prefix = job.uuid + ":";
    const auto session_prefix =
        account_prefix + job.epoch + ":" + std::to_string(job.authorization_generation) + ":preview:";
    for (auto item = preview_reservations_.begin(); item != preview_reservations_.end();) {
      if (item->first.starts_with(account_prefix) && !item->first.starts_with(session_prefix)) {
        preview_reserved_bytes_ -= item->second;
        item = preview_reservations_.erase(item);
      } else {
        ++item;
      }
    }
    if (preview_reservations_.contains(job.key))
      return "pending";
    if (job.preview_size == 0 || job.preview_size > config_.preview_max_bytes ||
        job.preview_size > config_.preview_total_bytes - std::min(preview_reserved_bytes_, config_.preview_total_bytes))
      return "saturated";
    preview_reservations_.emplace(job.key, job.preview_size);
    preview_reserved_bytes_ += job.preview_size;
  }
  refresh_keys_.insert(job.key);
  refresh_queue_.push_back(std::move(job));
  refresh_condition_.notify_one();
  return "queued";
}

void ReadModelService::prepare_preview(nlohmann::json &item, const std::string &uuid, const std::string &generation,
                                       const std::string &epoch, std::uint64_t authorization_generation) {
  if (!item.value("content", nlohmann::json(nullptr)).is_object())
    return;
  auto &content = item["content"];
  const auto file_id = content.value("preview_file_id", 0);
  const auto size = content.value("preview_size", std::int64_t{0});
  content.erase("preview_file_id");
  content.erase("preview_size");
  const auto mime = content.value("preview_mime", std::string{});
  content.erase("preview_mime");
  if (file_id <= 0 || size <= 0 || static_cast<std::uint64_t>(size) > config_.preview_max_bytes ||
      static_cast<std::uint64_t>(size) > config_.preview_total_bytes ||
      (mime != "image/jpeg" && mime != "image/png" && mime != "image/webp"))
    return;
  const auto chat_id = std::stoll(item.value("chat_id", "0"));
  const auto message_id = std::stoll(item.value("id", "0"));
  const ReadFence fence{generation, epoch, 0, authorization_generation, 0};
  content["preview_id"] = preview_token(config_, uuid, fence, chat_id, message_id, file_id);
  const auto key =
      uuid + ":" + epoch + ":" + std::to_string(authorization_generation) + ":preview:" + std::to_string(file_id);
  enqueue_refresh(
      {uuid, epoch, authorization_generation, chat_id, 0, 0, 0, key, file_id, static_cast<std::size_t>(size)});
}

void ReadModelService::refresh_loop() {
  while (true) {
    RefreshJob job;
    {
      std::unique_lock lock(refresh_mutex_);
      refresh_condition_.wait(lock, [this] { return refresh_stopping_ || !refresh_queue_.empty(); });
      if (refresh_stopping_)
        return;
      job = std::move(refresh_queue_.front());
      refresh_queue_.pop_front();
    }
    try {
      std::optional<ReadFence> fence;
      {
        std::lock_guard lock(context_.mutex_);
        const auto account = context_.accounts_.find(job.uuid);
        if (account != context_.accounts_.end() && account->second.runtime_epoch == job.epoch &&
            account->second.authorization_generation == job.authorization_generation)
          fence = read_fence(account->second);
      }
      if (fence) {
        td_api::object_ptr<td_api::Object> response;
        if (job.preview_file_id != 0)
          response = broker_.request(fence->client,
                                     td_api::make_object<td_api::downloadFile>(
                                         job.preview_file_id, 8, 0, static_cast<std::int64_t>(job.preview_size), false),
                                     std::chrono::seconds(2));
        else if (job.message_id != 0)
          response =
              broker_.request(fence->client, td_api::make_object<td_api::getMessage>(job.chat_id, job.message_id),
                              std::chrono::seconds(6));
        else
          response = broker_.request(fence->client,
                                     td_api::make_object<td_api::getChatHistory>(
                                         job.chat_id, job.anchor, 0, static_cast<std::int32_t>(job.limit), false),
                                     std::chrono::seconds(6));
        std::vector<nlohmann::json> items;
        if (response && response->get_id() == td_api::message::ID)
          items.push_back(message_projection(static_cast<const td_api::message &>(*response)));
        else if (response && response->get_id() == td_api::messages::ID)
          for (const auto &message : static_cast<const td_api::messages &>(*response).messages_)
            if (message)
              items.push_back(message_projection(*message));
        if (!items.empty()) {
          std::lock_guard lock(context_.mutex_);
          const auto account = context_.accounts_.find(job.uuid);
          if (account != context_.accounts_.end() && fence_valid(account->second, *fence)) {
            for (const auto &item : items) {
              const auto id = std::stoll(item.value("id", "0"));
              const auto changed = std::any_of(account->second.events.begin(), account->second.events.end(),
                                               [fence, &job, id](const auto &event) {
                                                 return event.value("sequence", 0ULL) > fence->sequence &&
                                                        event.value("chat_id", "") == std::to_string(job.chat_id) &&
                                                        event.value("message_id", "") == std::to_string(id);
                                               });
              if (!changed) {
                auto decorated = item;
                decorate_sender(account->second, decorated);
                context_.put_message(account->second, {job.chat_id, id}, std::move(decorated));
                UpdateJournal::append(account->second, "message_changed", job.chat_id, id);
              }
            }
          }
        }
      }
    } catch (const std::exception &) {
      // The local response has already completed. A later GET can retry refresh.
    }
    {
      std::lock_guard lock(refresh_mutex_);
      refresh_keys_.erase(job.key);
    }
  }
}

nlohmann::json ReadModelService::chats(const std::string &uuid, const std::string &list, std::size_t limit,
                                       const std::string &cursor) {
  ReadFence fence;
  std::uint64_t order_version = 0;
  std::optional<std::pair<std::int64_t, std::int64_t>> boundary;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !read_fence(found->second))
      return safe_error("service.busy", 503);
    fence = *read_fence(found->second);
    order_version = list == "archive" ? found->second.archive_order_version : found->second.main_order_version;
  }
  if (!cursor.empty()) {
    const auto dot = cursor.rfind('.');
    const auto payload = dot == std::string::npos ? std::string{} : cursor.substr(0, dot);
    const auto signature = sha256_hex(config_.internal_token + ":" + uuid + ":" + fence.generation + ":" + payload);
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
      if (dot == std::string::npos || !token_matches(signature, cursor.substr(dot + 1)) || fields.size() != 8 ||
          fields[0] != "l" || std::stoull(fields[1]) != fence.authorization_generation || fields[2] != fence.epoch ||
          fields[3] != list || std::stoull(fields[5]) < now)
        return safe_error("cursor.unusable", 409);
      if (std::stoull(fields[4]) != order_version)
        return safe_error("sync.resync_required", 409);
      boundary = {std::stoll(fields[6]), std::stoll(fields[7])};
    } catch (const std::exception &) {
      return safe_error("cursor.unusable", 409);
    }
  }
  bool load_failed = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  for (int attempt = 0; attempt < 8 && std::chrono::steady_clock::now() < deadline; ++attempt) {
    {
      std::lock_guard lock(context_.mutex_);
      const auto found = context_.accounts_.find(uuid);
      if (found == context_.accounts_.end() || !fence_valid(found->second, fence))
        return safe_error("sync.resync_required", 409);
      const auto &account = found->second;
      if (!cursor.empty() &&
          (list == "archive" ? account.archive_order_version : account.main_order_version) != order_version)
        return safe_error("sync.resync_required", 409);
      std::size_t available = 0;
      for (const auto &[id, projection] : account.chats) {
        if (!projection.value("positions", nlohmann::json::object()).contains(list))
          continue;
        const auto order = std::stoll(projection["positions"][list].value("order", "0"));
        if (!boundary || order < boundary->first || (order == boundary->first && id < boundary->second))
          ++available;
      }
      if (available > limit || (list == "archive" ? account.archive_exhausted : account.main_exhausted))
        break;
    }
    td_api::object_ptr<td_api::ChatList> chat_list =
        list == "archive" ? td_api::object_ptr<td_api::ChatList>(td_api::make_object<td_api::chatListArchive>())
                          : td_api::object_ptr<td_api::ChatList>(td_api::make_object<td_api::chatListMain>());
    const auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::seconds(0)) {
      load_failed = true;
      break;
    }
    const auto response =
        broker_.request(fence.client, td_api::make_object<td_api::loadChats>(std::move(chat_list), 50),
                        std::min(std::chrono::seconds(2), remaining));
    if (response && response->get_id() == td_api::error::ID &&
        static_cast<const td_api::error &>(*response).code_ == 404) {
      std::lock_guard lock(context_.mutex_);
      const auto found = context_.accounts_.find(uuid);
      if (found != context_.accounts_.end() && fence_valid(found->second, fence))
        (list == "archive" ? found->second.archive_exhausted : found->second.main_exhausted) = true;
      break;
    }
    if (!response || response->get_id() == td_api::error::ID) {
      load_failed = true;
      break;
    }
  }
  std::vector<nlohmann::json> items;
  std::uint64_t current_order_version = 0;
  std::string updates_cursor;
  bool exhausted = false;
  std::string connection;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !fence_valid(found->second, fence))
      return safe_error("sync.resync_required", 409);
    const auto &account = found->second;
    current_order_version = list == "archive" ? account.archive_order_version : account.main_order_version;
    if (!cursor.empty() && current_order_version != order_version)
      return safe_error("sync.resync_required", 409);
    for (const auto &[chat_id, projection] : account.chats) {
      static_cast<void>(chat_id);
      if (projection.value("positions", nlohmann::json::object()).contains(list))
        items.push_back(projection);
    }
    exhausted = list == "archive" ? account.archive_exhausted : account.main_exhausted;
    connection = account.connection_state;
    updates_cursor = cursors_.encode(account, fence.sequence);
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
    const auto payload = "l:" + std::to_string(fence.authorization_generation) + ":" + fence.epoch + ":" + list + ":" +
                         std::to_string(current_order_version) + ":" + std::to_string(expires) + ":" +
                         last["positions"][list].value("order", "0") + ":" + last.value("id", "0");
    next_cursor =
        payload + "." + sha256_hex(config_.internal_token + ":" + uuid + ":" + fence.generation + ":" + payload);
  }
  if (truncated)
    items.resize(limit);
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !fence_valid(found->second, fence) ||
        (list == "archive" ? found->second.archive_order_version : found->second.main_order_version) !=
            current_order_version)
      return safe_error("sync.resync_required", 409);
  }
  return {{"items", items},
          {"stale", true},
          {"partial", load_failed || (!truncated && !exhausted && items.size() < limit)},
          {"has_more", truncated   ? nlohmann::json(true)
                       : exhausted ? nlohmann::json(false)
                                   : nlohmann::json(nullptr)},
          {"next_cursor", next_cursor},
          {"retry_cursor",
           load_failed ? (cursor.empty() ? nlohmann::json(nullptr) : nlohmann::json(cursor)) : nlohmann::json(nullptr)},
          {"updates_cursor", updates_cursor},
          {"observed_at", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
          {"source", "tdlib_memory"},
          {"connection", connection},
          {"fallback_reason", load_failed ? nlohmann::json("load_failed") : nlohmann::json(nullptr)}};
}

nlohmann::json ReadModelService::chat(const std::string &uuid, std::int64_t chat_id) const {
  std::lock_guard lock(context_.mutex_);
  const auto account = context_.accounts_.find(uuid);
  if (account == context_.accounts_.end() || !read_fence(account->second))
    return safe_error("authorization.invalid_state", 409);
  const auto found = account->second.chats.find(chat_id);
  if (found == account->second.chats.end())
    return safe_error("chat.not_found", 404);
  auto item = found->second;
  if (item.value("last_message", nlohmann::json(nullptr)).is_object())
    decorate_sender(account->second, item["last_message"]);
  return {{"item", item},
          {"stale", true},
          {"partial", false},
          {"updates_cursor", cursors_.encode(account->second, account->second.event_sequence)},
          {"source", "tdlib_memory"}};
}

nlohmann::json ReadModelService::messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit,
                                          const std::string &cursor) {
  ReadFence fence;
  std::int64_t anchor = 0;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !read_fence(account->second))
      return safe_error("authorization.invalid_state", 409);
    fence = *read_fence(account->second);
  }
  if (!cursor.empty()) {
    const auto dot = cursor.rfind('.');
    const std::string prefix =
        "h:" + std::to_string(fence.authorization_generation) + ":" + fence.epoch + ":" + std::to_string(chat_id) + ":";
    if (dot == std::string::npos || !cursor.starts_with(prefix))
      return safe_error("cursor.unusable", 409);
    const auto payload = cursor.substr(0, dot);
    const auto signature = sha256_hex(config_.internal_token + ":" + uuid + ":" + fence.generation + ":" + payload);
    if (!token_matches(signature, cursor.substr(dot + 1)))
      return safe_error("cursor.unusable", 409);
    try {
      anchor = std::stoll(payload.substr(prefix.size()));
    } catch (const std::exception &) {
      return safe_error("cursor.unusable", 409);
    }
  }
  std::vector<nlohmann::json> items;
  std::unordered_set<std::int64_t> seen;
  std::int64_t fetch_anchor = anchor;
  bool local_exhausted = false;
  bool local_error = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  for (int attempt = 0; attempt < 8 && items.size() < limit; ++attempt) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::seconds(0)) {
      local_error = true;
      break;
    }
    const auto requested =
        static_cast<std::int32_t>(std::min<std::size_t>(100, limit - items.size() + (fetch_anchor == 0 ? 0 : 1)));
    auto response = broker_.request(
        fence.client, td_api::make_object<td_api::getChatHistory>(chat_id, fetch_anchor, 0, requested, true),
        std::min(std::chrono::seconds(2), remaining));
    if (!response || response->get_id() != td_api::messages::ID) {
      local_error = true;
      break;
    }
    auto &history = static_cast<td_api::messages &>(*response);
    if (history.messages_.empty()) {
      local_exhausted = true;
      break;
    }
    std::int64_t oldest = fetch_anchor;
    bool advanced = false;
    for (const auto &item : history.messages_) {
      if (!item || (fetch_anchor != 0 && item->id_ >= fetch_anchor))
        continue;
      if (!seen.insert(item->id_).second)
        continue;
      items.push_back(message_projection(*item));
      oldest = advanced ? std::min(oldest, item->id_) : item->id_;
      advanced = true;
    }
    if (!advanced)
      break;
    fetch_anchor = oldest;
  }
  nlohmann::json result;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !fence_valid(account->second, fence))
      return safe_error("sync.resync_required", 409);
    for (auto &item : items) {
      decorate_sender(account->second, item);
      const auto id = std::stoll(item.value("id", "0"));
      const auto changed = std::find_if(account->second.events.rbegin(), account->second.events.rend(),
                                        [&fence, chat_id, id](const auto &event) {
                                          return event.value("sequence", 0ULL) > fence.sequence &&
                                                 event.value("chat_id", "") == std::to_string(chat_id) &&
                                                 event.value("message_id", "") == std::to_string(id);
                                        });
      if (changed != account->second.events.rend()) {
        if (changed->value("type", "") == "message_deleted")
          item = nullptr;
        else if (const auto current = account->second.messages.find({chat_id, id});
                 current != account->second.messages.end())
          item = current->second;
      } else {
        context_.put_message(account->second, {chat_id, id}, item);
      }
    }
    items.erase(std::remove(items.begin(), items.end(), nullptr), items.end());
    std::sort(items.begin(), items.end(), [](const auto &left, const auto &right) {
      return std::stoll(left.value("id", "0")) > std::stoll(right.value("id", "0"));
    });
    items.erase(std::unique(items.begin(), items.end(),
                            [](const auto &left, const auto &right) {
                              return left.value("id", "0") == right.value("id", "0");
                            }),
                items.end());
    const bool full = items.size() >= limit;
    if (items.size() > limit)
      items.resize(limit);
    for (auto &item : items)
      prepare_preview(item, uuid, fence.generation, fence.epoch, fence.authorization_generation);
    const auto make_cursor = [&](std::int64_t id) {
      const auto payload = "h:" + std::to_string(fence.authorization_generation) + ":" + fence.epoch + ":" +
                           std::to_string(chat_id) + ":" + std::to_string(id);
      return payload + "." + sha256_hex(config_.internal_token + ":" + uuid + ":" + fence.generation + ":" + payload);
    };
    const auto last_id = items.empty() ? anchor : std::stoll(items.back().value("id", "0"));
    const bool progressed = !items.empty() && (anchor == 0 || last_id < anchor);
    const bool partial = !full || local_error;
    result = {{"items", items},
              {"stale", true},
              {"partial", partial},
              {"has_more", nullptr},
              {"local_exhausted", local_exhausted},
              {"next_cursor", progressed ? nlohmann::json(make_cursor(last_id)) : nlohmann::json(nullptr)},
              {"retry_cursor",
               partial ? nlohmann::json(cursor.empty() ? make_cursor(anchor) : cursor) : nlohmann::json(nullptr)},
              {"updates_cursor", cursors_.encode(account->second, fence.sequence)},
              {"observed_at", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
              {"source", "tdlib_local"},
              {"refresh", "pending"},
              {"connection", account->second.connection_state}};
  }
  const auto key = uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) +
                   ":history:" + std::to_string(chat_id) + ":" + std::to_string(anchor);
  result["refresh"] =
      enqueue_refresh({uuid, fence.epoch, fence.authorization_generation, chat_id, 0, anchor, limit, key});
  return result;
}

nlohmann::json ReadModelService::message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id) {
  ReadFence fence;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !read_fence(account->second))
      return safe_error("authorization.invalid_state", 409);
    fence = *read_fence(account->second);
    const auto found = account->second.messages.find({chat_id, message_id});
    if (found != account->second.messages.end()) {
      auto item = found->second;
      decorate_sender(account->second, item);
      prepare_preview(item, uuid, fence.generation, fence.epoch, fence.authorization_generation);
      const auto key = uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) +
                       ":message:" + std::to_string(chat_id) + ":" + std::to_string(message_id);
      const auto refresh =
          enqueue_refresh({uuid, fence.epoch, fence.authorization_generation, chat_id, message_id, 0, 1, key});
      return {{"item", item},
              {"stale", true},
              {"partial", false},
              {"updates_cursor", cursors_.encode(account->second, account->second.event_sequence)},
              {"source", "tdlib_memory"},
              {"refresh", refresh},
              {"connection", account->second.connection_state}};
    }
  }
  const auto response = broker_.request(
      fence.client, td_api::make_object<td_api::getMessageLocally>(chat_id, message_id), std::chrono::seconds(2));
  const auto key = uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) +
                   ":message:" + std::to_string(chat_id) + ":" + std::to_string(message_id);
  const auto refresh =
      enqueue_refresh({uuid, fence.epoch, fence.authorization_generation, chat_id, message_id, 0, 1, key});
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end() || !fence_valid(found->second, fence))
    return safe_error("sync.resync_required", 409);
  auto &account = found->second;
  if (!response)
    return safe_error("service.busy", 503);
  if (response->get_id() == td_api::error::ID) {
    const auto code = static_cast<const td_api::error &>(*response).code_;
    return code == 404   ? safe_error("message.cache_miss", 404)
           : code == 504 ? safe_error("read.deadline", 504)
                         : safe_error("service.busy", 503);
  }
  if (response->get_id() != td_api::message::ID)
    return safe_error("service.busy", 503);
  auto projection = message_projection(static_cast<const td_api::message &>(*response));
  decorate_sender(account, projection);
  const auto changed_after_read =
      std::any_of(account.events.begin(), account.events.end(), [fence, chat_id, message_id](const auto &event) {
        return event.value("sequence", 0ULL) > fence.sequence &&
               event.value("chat_id", "") == std::to_string(chat_id) &&
               event.value("message_id", "") == std::to_string(message_id);
      });
  if (changed_after_read) {
    if (const auto current = account.messages.find({chat_id, message_id}); current != account.messages.end()) {
      auto item = current->second;
      decorate_sender(account, item);
      prepare_preview(item, uuid, fence.generation, fence.epoch, fence.authorization_generation);
      return {{"item", item},
              {"stale", true},
              {"partial", false},
              {"updates_cursor", cursors_.encode(account, fence.sequence)},
              {"source", "tdlib_memory"},
              {"refresh", refresh},
              {"connection", account.connection_state}};
    }
    return safe_error("sync.resync_required", 409);
  }
  context_.put_message(account, {chat_id, message_id}, projection);
  prepare_preview(projection, uuid, fence.generation, fence.epoch, fence.authorization_generation);
  return {{"item", projection},
          {"stale", true},
          {"partial", false},
          {"updates_cursor", cursors_.encode(account, account.event_sequence)},
          {"source", "tdlib_local"},
          {"refresh", refresh},
          {"connection", account.connection_state}};
}

nlohmann::json ReadModelService::preview(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id,
                                         const std::string &preview_id) {
  ReadFence fence;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !read_fence(account->second))
      return safe_error("authorization.invalid_state", 409);
    fence = *read_fence(account->second);
  }
  if (preview_id.size() != 64)
    return safe_error("message.cache_miss", 404);
  const auto source = broker_.request(fence.client, td_api::make_object<td_api::getMessageLocally>(chat_id, message_id),
                                      std::chrono::seconds(2));
  if (!source || source->get_id() != td_api::message::ID)
    return safe_error("message.cache_miss", 404);
  const auto projection = message_projection(static_cast<const td_api::message &>(*source));
  const auto content = projection.value("content", nlohmann::json::object());
  const auto file_id = content.value("preview_file_id", 0);
  const auto size = content.value("preview_size", std::int64_t{0});
  const auto mime = content.value("preview_mime", std::string{});
  if (file_id <= 0 || size <= 0 || static_cast<std::uint64_t>(size) > config_.preview_max_bytes ||
      (mime != "image/jpeg" && mime != "image/png" && mime != "image/webp") ||
      !token_matches(preview_token(config_, uuid, fence, chat_id, message_id, file_id), preview_id))
    return safe_error("message.cache_miss", 404);
  const auto file_response =
      broker_.request(fence.client, td_api::make_object<td_api::getFile>(file_id), std::chrono::seconds(2));
  if (!file_response || file_response->get_id() != td_api::file::ID)
    return safe_error("message.cache_miss", 404);
  const auto &file = static_cast<const td_api::file &>(*file_response);
  if (!file.local_ || !file.local_->is_downloading_completed_ || file.size_ <= 0 || file.size_ > size ||
      static_cast<std::uint64_t>(file.size_) > config_.preview_max_bytes)
    return safe_error("message.cache_miss", 404);
  const auto part = broker_.request(fence.client, td_api::make_object<td_api::readFilePart>(file_id, 0, file.size_),
                                    std::chrono::seconds(2));
  if (!part || part->get_id() != td_api::data::ID)
    return safe_error("message.cache_miss", 404);
  const auto &bytes = static_cast<const td_api::data &>(*part).data_;
  if (bytes.empty() || bytes.size() > config_.preview_max_bytes || bytes.size() != file.size_)
    return safe_error("message.cache_miss", 404);
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !fence_valid(account->second, fence))
      return safe_error("sync.resync_required", 409);
    if (std::any_of(account->second.events.begin(), account->second.events.end(),
                    [&fence, chat_id, message_id](const auto &event) {
                      return event.value("sequence", 0ULL) > fence.sequence &&
                             event.value("chat_id", "") == std::to_string(chat_id) &&
                             event.value("message_id", "") == std::to_string(message_id);
                    }))
      return safe_error("message.cache_miss", 404);
  }
  return {{"mime_type", mime}, {"bytes_base64", base64_encode(bytes)}};
}

nlohmann::json ReadModelService::updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const {
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end() || !read_fence(found->second))
    return safe_error("authorization.invalid_state", 409);
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
