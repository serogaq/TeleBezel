#include "read_model_internal.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/status.hpp"
#include <algorithm>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace {
constexpr unsigned media_max_attempts = 4;
constexpr std::size_t media_entries_retained = 4096;
constexpr std::size_t album_limit = 10;
constexpr std::int32_t album_window = 9;
constexpr std::int32_t download_priority = 24;
constexpr auto media_abandoned = std::chrono::seconds(30);
constexpr auto media_call = std::chrono::seconds(2);

nlohmann::json media_state(nlohmann::json result, const char *state, std::int64_t retry_after = 0) {
  result["state"] = state;
  result["retry_after"] = retry_after > 0 ? nlohmann::json(retry_after) : nlohmann::json(nullptr);
  return result;
}

std::vector<const td_api::message *> album_members(const td_api::message &anchor, const td_api::Object *history) {
  std::vector<const td_api::message *> members;
  if (history != nullptr && history->get_id() == td_api::messages::ID) {
    for (const auto &message : static_cast<const td_api::messages &>(*history).messages_)
      if (message && message->media_album_id_ == anchor.media_album_id_ && message->id_ != anchor.id_)
        members.push_back(message.get());
  }
  members.push_back(&anchor);
  std::sort(members.begin(), members.end(), [](const auto *left, const auto *right) { return left->id_ < right->id_; });
  members.erase(std::unique(members.begin(), members.end(),
                            [](const auto *left, const auto *right) { return left->id_ == right->id_; }),
                members.end());
  if (members.size() > album_limit)
    members.resize(album_limit);
  return members;
}
} // namespace

std::size_t ReadModelService::media_entry_count() {
  std::lock_guard lock(refresh_mutex_);
  return media_entries_.size();
}

void ReadModelService::drop_media(const std::string &key) {
  const auto found = media_entries_.find(key);
  if (found == media_entries_.end())
    return;
  media_reserved_bytes_ -= found->second.reserved;
  media_cache_bytes_ -= found->second.ready_bytes;
  media_entries_.erase(found);
}

void ReadModelService::invalidate_media(const std::string &key) {
  std::lock_guard lock(refresh_mutex_);
  const auto found = media_entries_.find(key);
  if (found != media_entries_.end() && found->second.state == MediaEntry::State::ready)
    drop_media(key);
}

void ReadModelService::release_media(const std::string &key, bool ready, std::size_t bytes) {
  const auto found = media_entries_.find(key);
  if (found == media_entries_.end())
    return;
  auto &entry = found->second;
  media_reserved_bytes_ -= entry.reserved;
  entry.reserved = 0;
  if (ready) {
    entry.state = MediaEntry::State::ready;
    entry.ready_bytes = bytes;
    entry.used_at = std::chrono::steady_clock::now();
    media_cache_bytes_ += bytes;
    entry.attempts = 0;
    return;
  }
  entry.state = MediaEntry::State::failed;
  entry.ready_bytes = 0;
  ++entry.attempts;
  entry.retry_at = std::chrono::steady_clock::now() +
                   std::min<std::chrono::seconds>(std::chrono::seconds(1U << std::min(entry.attempts, 5U)),
                                                  std::chrono::seconds(config_.preview_failure_ttl_seconds));
}

void ReadModelService::queue_job(RefreshJob job) {
  auto &queue = refresh_queues_[job.uuid];
  if (queue.empty())
    refresh_rotation_.push_back(job.uuid);
  queue.push_back(std::move(job));
  refresh_condition_.notify_one();
}

void ReadModelService::trim_media_entries(std::chrono::steady_clock::time_point now, const std::string &uuid,
                                          const ReadFence &fence) {
  const auto ttl = std::chrono::seconds(config_.preview_failure_ttl_seconds);
  for (auto item = media_entries_.begin(); item != media_entries_.end();) {
    const auto &entry = item->second;
    const bool stale_session =
        entry.uuid == uuid && (entry.generation != fence.generation || entry.epoch != fence.epoch ||
                               entry.authorization_generation != fence.authorization_generation);
    const bool abandoned = entry.state == MediaEntry::State::downloading && now - entry.used_at >= media_abandoned;
    const bool forgotten = entry.state == MediaEntry::State::failed && now - entry.retry_at >= ttl;
    if (!stale_session && !abandoned && !forgotten) {
      ++item;
      continue;
    }
    if (entry.state != MediaEntry::State::failed) {
      RefreshJob eviction;
      eviction.kind = RefreshKind::evict;
      eviction.uuid = entry.uuid;
      eviction.key = "evict:" + item->first;
      eviction.file_id = entry.file_id;
      eviction.client = entry.client;
      queue_job(std::move(eviction));
    }
    media_reserved_bytes_ -= entry.reserved;
    media_cache_bytes_ -= entry.ready_bytes;
    item = media_entries_.erase(item);
  }
  while (media_entries_.size() >= media_entries_retained) {
    auto victim = media_entries_.end();
    for (auto item = media_entries_.begin(); item != media_entries_.end(); ++item) {
      if (item->second.state == MediaEntry::State::downloading)
        continue;
      if (victim == media_entries_.end() || item->second.used_at < victim->second.used_at)
        victim = item;
    }
    if (victim == media_entries_.end())
      return;
    drop_media(victim->first);
  }
}

bool ReadModelService::reserve_media(const std::string &key, const std::string &uuid, const ReadFence &fence,
                                     std::int32_t file_id, std::size_t size) {
  if (size == 0 || size > config_.preview_max_bytes || size > config_.preview_total_bytes)
    return false;
  // Ready files are evictable, running downloads are not.
  while (media_reserved_bytes_ + media_cache_bytes_ + size > config_.preview_total_bytes) {
    auto victim = media_entries_.end();
    for (auto item = media_entries_.begin(); item != media_entries_.end(); ++item) {
      if (item->first == key || item->second.state != MediaEntry::State::ready)
        continue;
      if (victim == media_entries_.end() || item->second.used_at < victim->second.used_at)
        victim = item;
    }
    if (victim == media_entries_.end())
      return false;
    RefreshJob eviction;
    eviction.kind = RefreshKind::evict;
    eviction.uuid = victim->second.uuid;
    eviction.key = "evict:" + victim->first;
    eviction.file_id = victim->second.file_id;
    eviction.client = victim->second.client;
    drop_media(victim->first);
    queue_job(std::move(eviction));
  }
  const auto attempts = media_entries_.contains(key) ? media_entries_[key].attempts : 0U;
  drop_media(key);
  auto &entry = media_entries_[key];
  entry.uuid = uuid;
  entry.generation = fence.generation;
  entry.epoch = fence.epoch;
  entry.authorization_generation = fence.authorization_generation;
  entry.client = fence.client;
  entry.file_id = file_id;
  entry.state = MediaEntry::State::downloading;
  entry.reserved = size;
  entry.attempts = attempts;
  entry.used_at = std::chrono::steady_clock::now();
  media_reserved_bytes_ += size;
  return true;
}

nlohmann::json ReadModelService::media(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id,
                                       std::size_t index, std::int32_t min_side, bool reveal, bool bytes) {
  const auto opened = begin_read(uuid);
  if (!opened)
    return safe_error("authorization.invalid_state", 409);
  const auto &fence = *opened;
  const auto source =
      broker_.request(fence.client, td_api::make_object<td_api::getMessageLocally>(chat_id, message_id), media_call);
  if (!source || source->get_id() != td_api::message::ID)
    return safe_error("message.cache_miss", 404);
  const auto &anchor = static_cast<const td_api::message &>(*source);
  td_api::object_ptr<td_api::Object> history;
  if (anchor.media_album_id_ != 0)
    history = broker_.request(
        fence.client,
        td_api::make_object<td_api::getChatHistory>(chat_id, message_id, -album_window, 2 * album_window + 1, true),
        media_call);
  const auto members = album_members(anchor, history.get());
  const bool inline_items = members.size() == 1;
  const auto count = inline_items ? media_info(anchor, min_side).items : members.size();
  if (index >= count)
    return safe_error("request.invalid", 422);
  const auto &item = inline_items ? anchor : *members[index];
  const auto info = media_info(item, min_side, inline_items ? index : 0);
  nlohmann::json result{{"index", index},
                        {"count", count},
                        {"item_message_id", std::to_string(item.id_)},
                        {"has_spoiler", info.has_spoiler},
                        {"fence",
                         {{"storage_generation", fence.generation},
                          {"authorization_generation", fence.authorization_generation},
                          {"runtime_epoch", fence.epoch}}}};
  if (info.restriction == "expired")
    return media_state(std::move(result), "unavailable");
  if (!info.restriction.empty())
    return media_state(std::move(result), "restricted");
  if (!info.source)
    return media_state(std::move(result), info.type == "none" ? "none" : "unsupported");
  if (info.has_spoiler && !reveal)
    return media_state(std::move(result), "spoiler");
  const auto file_id = info.source->file->id_;
  const auto declared = std::max(info.source->file->size_, info.source->file->expected_size_);
  if (file_id <= 0 || declared <= 0 || static_cast<std::uint64_t>(declared) > config_.preview_max_bytes)
    return media_state(std::move(result), "unsupported");
  result["source"] = {
      {"kind", info.source->kind},
      {"width", info.source->width},
      {"height", info.source->height},
      {"size", declared},
      {"unique_id", info.source->file->remote_ ? info.source->file->remote_->unique_id_ : std::string{}}};
  const auto key = reads::media_key(uuid, fence, file_id);
  const auto current = broker_.request(fence.client, td_api::make_object<td_api::getFile>(file_id), media_call);
  if (!current || current->get_id() != td_api::file::ID)
    return media_state(std::move(result), "downloading", 1);
  const auto &file = static_cast<const td_api::file &>(*current);
  const bool completed = file.local_ && file.local_->is_downloading_completed_ && file.size_ > 0 &&
                         static_cast<std::uint64_t>(file.size_) <= config_.preview_max_bytes;
  const bool active = file.local_ && file.local_->is_downloading_active_;
  bool start = false;
  std::int64_t wait = 1;
  const char *waiting = "downloading";
  {
    std::lock_guard lock(refresh_mutex_);
    const auto now = std::chrono::steady_clock::now();
    trim_media_entries(now, uuid, fence);
    auto found = media_entries_.find(key);
    if (completed) {
      if (found == media_entries_.end() &&
          reserve_media(key, uuid, fence, file_id, static_cast<std::size_t>(file.size_)))
        found = media_entries_.find(key);
      if (found != media_entries_.end()) {
        if (found->second.state != MediaEntry::State::ready)
          release_media(key, true, static_cast<std::size_t>(file.size_));
        found->second.used_at = now;
      }
    } else if (found != media_entries_.end() && found->second.state == MediaEntry::State::downloading && active) {
      found->second.used_at = now;
    } else {
      if (found != media_entries_.end() && found->second.state == MediaEntry::State::downloading)
        release_media(key, false, 0);
      found = media_entries_.find(key);
      if (found != media_entries_.end() && found->second.state == MediaEntry::State::failed &&
          (found->second.attempts >= media_max_attempts || now < found->second.retry_at)) {
        if (found->second.attempts >= media_max_attempts)
          waiting = "unavailable";
        wait = std::max<std::int64_t>(
            1, std::chrono::duration_cast<std::chrono::seconds>(found->second.retry_at - now).count());
      } else if (reserve_media(key, uuid, fence, file_id, static_cast<std::size_t>(declared))) {
        start = true;
      } else {
        wait = 2;
      }
    }
  }
  if (start) {
    // An asynchronous download keeps the shared refresh worker free; the
    // client learns about completion from the next request.
    const auto response = broker_.request(
        fence.client, td_api::make_object<td_api::downloadFile>(file_id, download_priority, 0, 0, false), media_call);
    if (!response || response->get_id() != td_api::file::ID) {
      std::lock_guard lock(refresh_mutex_);
      release_media(key, false, 0);
    }
  }
  if (!completed)
    return std::string(waiting) == "unavailable" ? media_state(std::move(result), "unavailable")
                                                 : media_state(std::move(result), waiting, wait);
  result = media_state(std::move(result), "ready");
  if (!bytes)
    return result;
  const auto part =
      broker_.request(fence.client, td_api::make_object<td_api::readFilePart>(file_id, 0, file.size_), media_call);
  if (!part || part->get_id() != td_api::data::ID) {
    invalidate_media(key);
    return media_state(std::move(result), "downloading", 1);
  }
  const auto &data = static_cast<const td_api::data &>(*part).data_;
  if (data.empty() || data.size() != static_cast<std::size_t>(file.size_)) {
    invalidate_media(key);
    return media_state(std::move(result), "downloading", 1);
  }
  {
    std::lock_guard lock(context_.mutex_);
    auto *const account = fenced_account(uuid, fence);
    if (account == nullptr)
      return safe_error("sync.resync_required", 409);
    if (reads::changed_since(*account, fence, chat_id, item.id_) != nullptr)
      return safe_error("message.cache_miss", 404);
  }
  result["bytes_base64"] = reads::base64_encode(data);
  return result;
}

} // namespace telebezel::runtime
