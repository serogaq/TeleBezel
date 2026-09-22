#include "telebezel/crypto.hpp"
#include "telebezel/parse.hpp"
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
constexpr const char *preview_purpose = "telebezel/preview/v1";
constexpr const char *chats_cursor_purpose = "telebezel/cursor/chats/v1";
constexpr const char *history_cursor_purpose = "telebezel/cursor/history/v1";
// A refresh that changed nothing is not repeated while this window lasts.
constexpr auto refresh_cooldown = std::chrono::seconds(5);
constexpr std::size_t refresh_queue_per_account = 32;
constexpr std::size_t refresh_results_retained = 512;
constexpr unsigned preview_max_attempts = 4;

std::string preview_token(const Config &config, const std::string &uuid, const ReadFence &fence, std::int64_t chat_id,
                          std::int64_t message_id, std::int32_t file_id) {
  return hmac_sha256_hex(config.internal_token, preview_purpose,
                         uuid + ":" + fence.generation + ":" + fence.epoch + ":" +
                             std::to_string(fence.authorization_generation) + ":" + std::to_string(chat_id) + ":" +
                             std::to_string(message_id) + ":" + std::to_string(file_id));
}

std::string cursor_signature(const Config &config, const char *purpose, const std::string &uuid,
                             const std::string &generation, const std::string &payload) {
  return hmac_sha256_hex(config.internal_token, purpose, uuid + ":" + generation + ":" + payload);
}

std::vector<std::string> split_fields(const std::string &payload, char separator) {
  std::vector<std::string> fields;
  std::size_t position = 0;
  while (position <= payload.size()) {
    const auto found = payload.find(separator, position);
    fields.push_back(payload.substr(position, found == std::string::npos ? std::string::npos : found - position));
    if (found == std::string::npos)
      break;
    position = found + 1;
  }
  return fields;
}

std::string base64_encode(const std::string &bytes) {
  std::string output(4 * ((bytes.size() + 2) / 3), '\0');
  const auto count =
      EVP_EncodeBlock(reinterpret_cast<unsigned char *>(output.data()),
                      reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<int>(bytes.size()));
  output.resize(static_cast<std::size_t>(count));
  return output;
}

std::int64_t projection_id(const nlohmann::json &item) {
  return parse_int64(item.value("id", std::string{"0"})).value_or(0);
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
  if (const auto queue = refresh_queues_.find(uuid); queue != refresh_queues_.end()) {
    for (const auto &job : queue->second)
      refresh_keys_.erase(job.key);
    refresh_queues_.erase(queue);
  }
  refresh_rotation_.erase(std::remove(refresh_rotation_.begin(), refresh_rotation_.end(), uuid),
                          refresh_rotation_.end());
  for (auto item = preview_entries_.begin(); item != preview_entries_.end();) {
    if (item->second.uuid != uuid) {
      ++item;
      continue;
    }
    preview_reserved_bytes_ -= item->second.reserved;
    preview_cache_bytes_ -= item->second.ready_bytes;
    item = preview_entries_.erase(item);
  }
  for (auto item = refresh_results_.begin(); item != refresh_results_.end();) {
    if (item->first.starts_with(uuid + ":"))
      item = refresh_results_.erase(item);
    else
      ++item;
  }
}

void ReadModelService::drop_preview(const std::string &key) {
  const auto found = preview_entries_.find(key);
  if (found == preview_entries_.end())
    return;
  preview_reserved_bytes_ -= found->second.reserved;
  preview_cache_bytes_ -= found->second.ready_bytes;
  preview_entries_.erase(found);
}

void ReadModelService::invalidate_preview(const std::string &key) {
  std::lock_guard lock(refresh_mutex_);
  const auto found = preview_entries_.find(key);
  if (found == preview_entries_.end() || found->second.state != PreviewEntry::State::ready)
    return;
  preview_cache_bytes_ -= found->second.ready_bytes;
  preview_entries_.erase(found);
}

void ReadModelService::release_preview(const std::string &key, bool ready, std::size_t bytes) {
  std::lock_guard lock(refresh_mutex_);
  const auto found = preview_entries_.find(key);
  if (found == preview_entries_.end())
    return;
  auto &entry = found->second;
  preview_reserved_bytes_ -= entry.reserved;
  entry.reserved = 0;
  if (ready) {
    entry.state = PreviewEntry::State::ready;
    entry.ready_bytes = bytes;
    entry.used_at = std::chrono::steady_clock::now();
    preview_cache_bytes_ += bytes;
    entry.attempts = 0;
    return;
  }
  entry.state = PreviewEntry::State::failed;
  entry.ready_bytes = 0;
  ++entry.attempts;
  entry.retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(1U << std::min(entry.attempts, 5U));
}

bool ReadModelService::reserve_preview(const RefreshJob &job) {
  if (job.preview_size == 0 || job.preview_size > config_.preview_max_bytes ||
      job.preview_size > config_.preview_total_bytes)
    return false;
  // Ready bytes are evictable, in-flight reservations are not.
  while (preview_reserved_bytes_ + preview_cache_bytes_ + job.preview_size > config_.preview_total_bytes) {
    auto victim = preview_entries_.end();
    for (auto item = preview_entries_.begin(); item != preview_entries_.end(); ++item) {
      if (item->first == job.key || item->second.state != PreviewEntry::State::ready)
        continue;
      if (victim == preview_entries_.end() || item->second.used_at < victim->second.used_at)
        victim = item;
    }
    if (victim == preview_entries_.end())
      return false;
    RefreshJob eviction;
    eviction.kind = RefreshKind::evict;
    eviction.uuid = victim->second.uuid;
    eviction.key = "evict:" + victim->first;
    eviction.preview_file_id = victim->second.file_id;
    eviction.client = victim->second.client;
    preview_cache_bytes_ -= victim->second.ready_bytes;
    preview_entries_.erase(victim);
    auto &queue = refresh_queues_[eviction.uuid];
    if (queue.empty())
      refresh_rotation_.push_back(eviction.uuid);
    queue.push_back(std::move(eviction));
    refresh_condition_.notify_one();
  }
  auto &entry = preview_entries_[job.key];
  entry.uuid = job.uuid;
  entry.client = job.client;
  entry.file_id = job.preview_file_id;
  entry.state = PreviewEntry::State::queued;
  entry.reserved = job.preview_size;
  entry.ready_bytes = 0;
  entry.used_at = std::chrono::steady_clock::now();
  preview_reserved_bytes_ += job.preview_size;
  return true;
}

std::string ReadModelService::enqueue_refresh(RefreshJob job) {
  std::lock_guard lock(refresh_mutex_);
  if (refresh_stopping_)
    return "stopped";
  if (refresh_keys_.contains(job.key))
    return "pending";
  const auto now = std::chrono::steady_clock::now();
  if (job.kind == RefreshKind::preview) {
    unsigned attempts = 0;
    if (const auto existing = preview_entries_.find(job.key); existing != preview_entries_.end()) {
      switch (existing->second.state) {
      case PreviewEntry::State::queued:
      case PreviewEntry::State::downloading:
        return "pending";
      case PreviewEntry::State::ready:
        existing->second.used_at = now;
        return "ready";
      case PreviewEntry::State::failed:
        if (now < existing->second.retry_at || existing->second.attempts >= preview_max_attempts)
          return "failed";
        break;
      }
      attempts = existing->second.attempts;
      drop_preview(job.key);
    }
    if (!reserve_preview(job))
      return "saturated";
    preview_entries_[job.key].attempts = attempts;
  } else if (const auto outcome = refresh_results_.find(job.key);
             outcome != refresh_results_.end() && now - outcome->second.at < refresh_cooldown) {
    return outcome->second.state;
  }
  auto &queue = refresh_queues_[job.uuid];
  if (queue.size() >= refresh_queue_per_account) {
    if (job.kind == RefreshKind::preview)
      drop_preview(job.key);
    if (queue.empty())
      refresh_queues_.erase(job.uuid);
    return "saturated";
  }
  if (queue.empty())
    refresh_rotation_.push_back(job.uuid);
  refresh_keys_.insert(job.key);
  queue.push_back(std::move(job));
  refresh_condition_.notify_one();
  return "queued";
}

void ReadModelService::prepare_preview(nlohmann::json &item, const std::string &uuid, const std::string &generation,
                                       const std::string &epoch, std::uint64_t authorization_generation,
                                       std::int32_t client) {
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
  const auto chat_id = parse_int64(item.value("chat_id", std::string{"0"})).value_or(0);
  const auto message_id = projection_id(item);
  const ReadFence fence{generation, epoch, 0, authorization_generation, 0};
  content["preview_id"] = preview_token(config_, uuid, fence, chat_id, message_id, file_id);
  RefreshJob job;
  job.kind = RefreshKind::preview;
  job.uuid = uuid;
  job.epoch = epoch;
  job.authorization_generation = authorization_generation;
  job.chat_id = chat_id;
  job.key = uuid + ":" + epoch + ":" + std::to_string(authorization_generation) + ":preview:" + std::to_string(file_id);
  job.preview_file_id = file_id;
  job.preview_size = static_cast<std::size_t>(size);
  job.client = client;
  content["preview_state"] = enqueue_refresh(std::move(job));
}

ReadModelService::HistoryPage ReadModelService::fetch_history(std::int32_t client, std::int64_t chat_id,
                                                              std::int64_t anchor, std::size_t limit, bool only_local,
                                                              std::chrono::steady_clock::time_point deadline) {
  // One page definition for the synchronous read and the background refresh:
  // the anchor is excluded, and short pages are topped up.
  HistoryPage page;
  std::unordered_set<std::int64_t> seen;
  std::int64_t fetch_anchor = anchor;
  for (int attempt = 0; attempt < 8 && page.items.size() < limit; ++attempt) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::seconds(0)) {
      page.failed = true;
      page.deadline = true;
      break;
    }
    const auto requested =
        static_cast<std::int32_t>(std::min<std::size_t>(100, limit - page.items.size() + (fetch_anchor == 0 ? 0 : 1)));
    auto response = broker_.request(
        client, td_api::make_object<td_api::getChatHistory>(chat_id, fetch_anchor, 0, requested, only_local),
        std::min(std::chrono::seconds(2), remaining));
    if (!response || response->get_id() != td_api::messages::ID) {
      page.failed = true;
      break;
    }
    auto &history = static_cast<td_api::messages &>(*response);
    if (history.messages_.empty()) {
      page.exhausted = true;
      break;
    }
    std::int64_t oldest = fetch_anchor;
    bool advanced = false;
    for (const auto &item : history.messages_) {
      if (!item || (fetch_anchor != 0 && item->id_ >= fetch_anchor))
        continue;
      if (!seen.insert(item->id_).second)
        continue;
      page.items.push_back(message_projection(*item));
      oldest = advanced ? std::min(oldest, item->id_) : item->id_;
      advanced = true;
    }
    if (!advanced)
      break;
    fetch_anchor = oldest;
  }
  return page;
}

bool ReadModelService::publish_projections(const RefreshJob &job, const ReadFence &fence,
                                           const std::vector<nlohmann::json> &items) {
  bool changed = false;
  std::lock_guard lock(context_.mutex_);
  const auto account = context_.accounts_.find(job.uuid);
  if (account == context_.accounts_.end() || !fence_valid(account->second, fence))
    return false;
  for (const auto &item : items) {
    const auto id = projection_id(item);
    const auto superseded = std::any_of(account->second.events.begin(), account->second.events.end(),
                                        [&fence, &job, id](const auto &event) {
                                          return event.value("sequence", 0ULL) > fence.sequence &&
                                                 event.value("chat_id", "") == std::to_string(job.chat_id) &&
                                                 event.value("message_id", "") == std::to_string(id);
                                        });
    if (superseded)
      continue;
    auto decorated = item;
    decorate_sender(account->second, decorated);
    const auto current = account->second.messages.find({job.chat_id, id});
    // Reading back the same projection is not a change; journalling it would
    // make every GET produce the event that triggers the next GET.
    if (current != account->second.messages.end() && current->second == decorated)
      continue;
    context_.put_message(account->second, {job.chat_id, id}, std::move(decorated));
    UpdateJournal::append(account->second, "message_changed", job.chat_id, id);
    changed = true;
  }
  return changed;
}

bool ReadModelService::run_refresh(const RefreshJob &job) {
  if (job.kind == RefreshKind::evict) {
    broker_.request(job.client, td_api::make_object<td_api::deleteFile>(job.preview_file_id), std::chrono::seconds(2));
    return false;
  }
  std::optional<ReadFence> fence;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(job.uuid);
    if (account != context_.accounts_.end() && account->second.runtime_epoch == job.epoch &&
        account->second.authorization_generation == job.authorization_generation)
      fence = read_fence(account->second);
  }
  if (!fence) {
    if (job.kind == RefreshKind::preview)
      release_preview(job.key, false, 0);
    return false;
  }
  if (job.kind == RefreshKind::preview) {
    {
      std::lock_guard lock(refresh_mutex_);
      if (const auto entry = preview_entries_.find(job.key); entry != preview_entries_.end())
        entry->second.state = PreviewEntry::State::downloading;
    }
    // downloadFile answers before the bytes have landed, so completion is
    // confirmed against the returned file object.
    auto response = broker_.request(fence->client,
                                    td_api::make_object<td_api::downloadFile>(
                                        job.preview_file_id, 8, 0, static_cast<std::int64_t>(job.preview_size), true),
                                    std::chrono::seconds(12));
    std::size_t downloaded = 0;
    bool completed = false;
    if (response && response->get_id() == td_api::file::ID) {
      const auto &file = static_cast<const td_api::file &>(*response);
      completed = file.local_ && file.local_->is_downloading_completed_ && file.local_->downloaded_size_ > 0 &&
                  static_cast<std::uint64_t>(file.local_->downloaded_size_) <= config_.preview_max_bytes;
      if (completed)
        downloaded = static_cast<std::size_t>(file.local_->downloaded_size_);
    }
    release_preview(job.key, completed, downloaded);
    return false;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  std::vector<nlohmann::json> items;
  if (job.kind == RefreshKind::message) {
    auto response = broker_.request(fence->client, td_api::make_object<td_api::getMessage>(job.chat_id, job.message_id),
                                    std::chrono::seconds(6));
    if (response && response->get_id() == td_api::message::ID)
      items.push_back(message_projection(static_cast<const td_api::message &>(*response)));
  } else {
    items = fetch_history(fence->client, job.chat_id, job.anchor, job.limit, false, deadline).items;
  }
  if (items.empty())
    return false;
  return publish_projections(job, *fence, items);
}

void ReadModelService::refresh_loop() {
  while (true) {
    RefreshJob job;
    {
      std::unique_lock lock(refresh_mutex_);
      refresh_condition_.wait(lock, [this] { return refresh_stopping_ || !refresh_rotation_.empty(); });
      if (refresh_stopping_)
        return;
      const auto uuid = refresh_rotation_.front();
      refresh_rotation_.pop_front();
      const auto queue = refresh_queues_.find(uuid);
      if (queue == refresh_queues_.end())
        continue;
      job = std::move(queue->second.front());
      queue->second.pop_front();
      if (queue->second.empty())
        refresh_queues_.erase(queue);
      else
        refresh_rotation_.push_back(uuid);
    }
    bool changed = false;
    try {
      changed = run_refresh(job);
    } catch (const std::exception &) {
      if (job.kind == RefreshKind::preview)
        release_preview(job.key, false, 0);
    }
    {
      std::lock_guard lock(refresh_mutex_);
      refresh_keys_.erase(job.key);
      if (job.kind == RefreshKind::history || job.kind == RefreshKind::message) {
        if (refresh_results_.size() >= refresh_results_retained)
          refresh_results_.clear();
        refresh_results_[job.key] = {changed ? "completed" : "unchanged", std::chrono::steady_clock::now()};
      }
    }
    context_.condition_.notify_all();
  }
}

nlohmann::json ReadModelService::chats(const std::string &uuid, const std::string &list, std::size_t limit,
                                       const std::string &cursor) {
  ReadFence fence;
  std::uint64_t cursor_version = 0;
  std::optional<std::pair<std::int64_t, std::int64_t>> boundary;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !read_fence(found->second))
      return safe_error("service.busy", 503);
    fence = *read_fence(found->second);
  }
  if (!cursor.empty()) {
    const auto dot = cursor.rfind('.');
    if (dot == std::string::npos)
      return safe_error("cursor.unusable", 409);
    const auto payload = cursor.substr(0, dot);
    const auto signature = cursor_signature(config_, chats_cursor_purpose, uuid, fence.generation, payload);
    const auto fields = split_fields(payload, ':');
    if (!token_matches(signature, cursor.substr(dot + 1)) || fields.size() != 8 || fields[0] != "l" ||
        fields[2] != fence.epoch || fields[3] != list)
      return safe_error("cursor.unusable", 409);
    const auto authorization = parse_uint64(fields[1]);
    const auto version = parse_uint64(fields[4]);
    const auto expires = parse_uint64(fields[5]);
    const auto order = parse_int64(fields[6]);
    const auto boundary_id = parse_int64(fields[7]);
    const auto now = static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    if (!authorization || !version || !expires || !order || !boundary_id ||
        *authorization != fence.authorization_generation || *expires < now)
      return safe_error("cursor.unusable", 409);
    cursor_version = *version;
    boundary = {*order, *boundary_id};
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !fence_valid(found->second, fence))
      return safe_error("sync.resync_required", 409);
    if (order_log(found->second, list).disturbed(cursor_version, boundary->first))
      return safe_error("sync.resync_required", 409);
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
      if (boundary && order_log(found->second, list).disturbed(cursor_version, boundary->first))
        return safe_error("sync.resync_required", 409);
      std::size_t available = 0;
      for (const auto &[id, projection] : account.chats) {
        if (!projection.value("positions", nlohmann::json::object()).contains(list))
          continue;
        const auto order = parse_int64(projection["positions"][list].value("order", std::string{"0"})).value_or(0);
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
  std::uint64_t current_version = 0;
  std::string updates_cursor;
  bool exhausted = false;
  std::string connection;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !fence_valid(found->second, fence))
      return safe_error("sync.resync_required", 409);
    if (boundary && order_log(found->second, list).disturbed(cursor_version, boundary->first))
      return safe_error("sync.resync_required", 409);
    const auto &account = found->second;
    current_version = order_log(found->second, list).version;
    for (const auto &[chat_id, projection] : account.chats) {
      static_cast<void>(chat_id);
      if (projection.value("positions", nlohmann::json::object()).contains(list))
        items.push_back(projection);
    }
    exhausted = list == "archive" ? account.archive_exhausted : account.main_exhausted;
    connection = account.connection_state;
    updates_cursor = cursors_.encode(account, fence.sequence);
  }
  const auto position_order = [&list](const nlohmann::json &item) {
    return parse_int64(item["positions"][list].value("order", std::string{"0"})).value_or(0);
  };
  std::sort(items.begin(), items.end(), [&position_order](const auto &left, const auto &right) {
    const auto left_order = position_order(left);
    const auto right_order = position_order(right);
    return left_order == right_order ? projection_id(left) > projection_id(right) : left_order > right_order;
  });
  if (boundary) {
    items.erase(items.begin(), std::find_if(items.begin(), items.end(), [&position_order, &boundary](const auto &item) {
                  const auto order = position_order(item);
                  return order < boundary->first ||
                         (order == boundary->first && projection_id(item) < boundary->second);
                }));
  }
  const bool truncated = items.size() > limit;
  nlohmann::json next_cursor = nullptr;
  if (truncated) {
    const auto &last = items[limit - 1];
    const auto expires = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + 900;
    const auto payload = "l:" + std::to_string(fence.authorization_generation) + ":" + fence.epoch + ":" + list + ":" +
                         std::to_string(current_version) + ":" + std::to_string(expires) + ":" +
                         last["positions"][list].value("order", "0") + ":" + last.value("id", "0");
    next_cursor = payload + "." + cursor_signature(config_, chats_cursor_purpose, uuid, fence.generation, payload);
    items.resize(limit);
  }
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || !fence_valid(found->second, fence))
      return safe_error("sync.resync_required", 409);
    if (boundary && order_log(found->second, list).disturbed(cursor_version, boundary->first))
      return safe_error("sync.resync_required", 409);
  }
  const bool partial = load_failed || (!truncated && !exhausted && items.size() < limit);
  return {{"items", items},
          {"stale", true},
          {"partial", partial},
          {"local_exhausted", exhausted},
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
    const auto signature = cursor_signature(config_, history_cursor_purpose, uuid, fence.generation, payload);
    if (!token_matches(signature, cursor.substr(dot + 1)))
      return safe_error("cursor.unusable", 409);
    const auto parsed = parse_int64(std::string_view(payload).substr(prefix.size()));
    if (!parsed)
      return safe_error("cursor.unusable", 409);
    anchor = *parsed;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  auto page = fetch_history(fence.client, chat_id, anchor, limit, true, deadline);
  auto items = std::move(page.items);
  nlohmann::json result;
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !fence_valid(account->second, fence))
      return safe_error("sync.resync_required", 409);
    for (auto &item : items) {
      decorate_sender(account->second, item);
      const auto id = projection_id(item);
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
    std::sort(items.begin(), items.end(),
              [](const auto &left, const auto &right) { return projection_id(left) > projection_id(right); });
    items.erase(std::unique(items.begin(), items.end(),
                            [](const auto &left, const auto &right) {
                              return left.value("id", "0") == right.value("id", "0");
                            }),
                items.end());
    const bool full = items.size() >= limit;
    if (items.size() > limit)
      items.resize(limit);
    for (auto &item : items)
      prepare_preview(item, uuid, fence.generation, fence.epoch, fence.authorization_generation, fence.client);
    const auto make_cursor = [&](std::int64_t id) {
      const auto payload = "h:" + std::to_string(fence.authorization_generation) + ":" + fence.epoch + ":" +
                           std::to_string(chat_id) + ":" + std::to_string(id);
      return payload + "." + cursor_signature(config_, history_cursor_purpose, uuid, fence.generation, payload);
    };
    const auto last_id = items.empty() ? anchor : projection_id(items.back());
    const bool progressed = !items.empty() && (anchor == 0 || last_id < anchor);
    // partial: the page could not be completed from local storage, unlike
    // local_exhausted, which says nothing older than the anchor exists locally.
    const bool partial = page.failed || (!full && !page.exhausted);
    result = {{"items", items},
              {"stale", true},
              {"partial", partial},
              {"has_more", progressed                   ? nlohmann::json(true)
                           : page.exhausted && !partial ? nlohmann::json(false)
                                                        : nlohmann::json(nullptr)},
              {"local_exhausted", page.exhausted},
              {"next_cursor", progressed ? nlohmann::json(make_cursor(last_id)) : nlohmann::json(nullptr)},
              {"retry_cursor",
               partial ? nlohmann::json(cursor.empty() ? make_cursor(anchor) : cursor) : nlohmann::json(nullptr)},
              {"updates_cursor", cursors_.encode(account->second, fence.sequence)},
              {"observed_at", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
              {"source", "tdlib_local"},
              {"refresh", "pending"},
              {"fallback_reason", page.deadline ? nlohmann::json("read.deadline")
                                  : page.failed ? nlohmann::json("read.unavailable")
                                                : nlohmann::json(nullptr)},
              {"connection", account->second.connection_state}};
  }
  RefreshJob job;
  job.kind = RefreshKind::history;
  job.uuid = uuid;
  job.epoch = fence.epoch;
  job.authorization_generation = fence.authorization_generation;
  job.chat_id = chat_id;
  job.anchor = anchor;
  job.limit = limit;
  job.key = uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) +
            ":history:" + std::to_string(chat_id) + ":" + std::to_string(anchor);
  job.client = fence.client;
  result["refresh"] = enqueue_refresh(std::move(job));
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
  }
  const auto make_job = [&] {
    RefreshJob job;
    job.kind = RefreshKind::message;
    job.uuid = uuid;
    job.epoch = fence.epoch;
    job.authorization_generation = fence.authorization_generation;
    job.chat_id = chat_id;
    job.message_id = message_id;
    job.limit = 1;
    job.key = uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) +
              ":message:" + std::to_string(chat_id) + ":" + std::to_string(message_id);
    job.client = fence.client;
    return job;
  };
  {
    std::lock_guard lock(context_.mutex_);
    const auto account = context_.accounts_.find(uuid);
    if (account == context_.accounts_.end() || !fence_valid(account->second, fence))
      return safe_error("authorization.invalid_state", 409);
    const auto found = account->second.messages.find({chat_id, message_id});
    if (found != account->second.messages.end()) {
      auto item = found->second;
      decorate_sender(account->second, item);
      prepare_preview(item, uuid, fence.generation, fence.epoch, fence.authorization_generation, fence.client);
      const auto refresh = enqueue_refresh(make_job());
      return {{"item", item},
              {"stale", true},
              {"partial", false},
              {"updates_cursor", cursors_.encode(account->second, account->second.event_sequence)},
              {"source", "tdlib_memory"},
              {"refresh", refresh},
              {"fallback_reason", nullptr},
              {"connection", account->second.connection_state}};
    }
  }
  const auto response = broker_.request(
      fence.client, td_api::make_object<td_api::getMessageLocally>(chat_id, message_id), std::chrono::seconds(2));
  const auto refresh = enqueue_refresh(make_job());
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
      prepare_preview(item, uuid, fence.generation, fence.epoch, fence.authorization_generation, fence.client);
      return {{"item", item},
              {"stale", true},
              {"partial", false},
              {"updates_cursor", cursors_.encode(account, fence.sequence)},
              {"source", "tdlib_memory"},
              {"refresh", refresh},
              {"fallback_reason", nullptr},
              {"connection", account.connection_state}};
    }
    return safe_error("sync.resync_required", 409);
  }
  context_.put_message(account, {chat_id, message_id}, projection);
  prepare_preview(projection, uuid, fence.generation, fence.epoch, fence.authorization_generation, fence.client);
  return {{"item", projection},         {"stale", true},
          {"partial", false},           {"updates_cursor", cursors_.encode(account, account.event_sequence)},
          {"source", "tdlib_local"},    {"refresh", refresh},
          {"fallback_reason", nullptr}, {"connection", account.connection_state}};
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
  const auto key = uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) +
                   ":preview:" + std::to_string(file_id);
  const auto file_response =
      broker_.request(fence.client, td_api::make_object<td_api::getFile>(file_id), std::chrono::seconds(2));
  if (!file_response || file_response->get_id() != td_api::file::ID)
    return safe_error("message.cache_miss", 404);
  const auto &file = static_cast<const td_api::file &>(*file_response);
  if (!file.local_ || !file.local_->is_downloading_completed_ || file.size_ <= 0 || file.size_ > size ||
      static_cast<std::uint64_t>(file.size_) > config_.preview_max_bytes) {
    // The local copy is gone; drop the accounting so the next read re-queues.
    invalidate_preview(key);
    return safe_error("message.cache_miss", 404);
  }
  const auto part = broker_.request(fence.client, td_api::make_object<td_api::readFilePart>(file_id, 0, file.size_),
                                    std::chrono::seconds(2));
  if (!part || part->get_id() != td_api::data::ID) {
    invalidate_preview(key);
    return safe_error("message.cache_miss", 404);
  }
  const auto &bytes = static_cast<const td_api::data &>(*part).data_;
  // file.size_ is range-checked above, so widening it to std::size_t is exact.
  const auto expected_bytes = static_cast<std::size_t>(file.size_);
  if (bytes.empty() || bytes.size() > config_.preview_max_bytes || bytes.size() != expected_bytes) {
    invalidate_preview(key);
    return safe_error("message.cache_miss", 404);
  }
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
  {
    std::lock_guard lock(refresh_mutex_);
    if (const auto entry = preview_entries_.find(key); entry != preview_entries_.end())
      entry->second.used_at = std::chrono::steady_clock::now();
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
