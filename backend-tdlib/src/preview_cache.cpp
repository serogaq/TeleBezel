#include "read_model_internal.hpp"
#include "telebezel/parse.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/status.hpp"
#include <algorithm>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace {
constexpr unsigned preview_max_attempts = 4;
constexpr std::size_t preview_entries_retained = 4096;
} // namespace

std::size_t ReadModelService::preview_entry_count() {
  std::lock_guard lock(refresh_mutex_);
  return preview_entries_.size();
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
  entry.retry_at = std::chrono::steady_clock::now() +
                   std::min<std::chrono::seconds>(std::chrono::seconds(1U << std::min(entry.attempts, 5U)),
                                                  std::chrono::seconds(config_.preview_failure_ttl_seconds));
}

bool ReadModelService::trim_preview_entries(std::chrono::steady_clock::time_point now) {
  const auto ttl = std::chrono::seconds(config_.preview_failure_ttl_seconds);
  for (auto item = preview_entries_.begin(); item != preview_entries_.end();) {
    if (item->second.state == PreviewEntry::State::failed && now - item->second.retry_at >= ttl)
      item = preview_entries_.erase(item);
    else
      ++item;
  }
  while (preview_entries_.size() >= preview_entries_retained) {
    auto victim = preview_entries_.end();
    for (auto item = preview_entries_.begin(); item != preview_entries_.end(); ++item) {
      if (item->second.state != PreviewEntry::State::failed)
        continue;
      if (victim == preview_entries_.end() || item->second.retry_at < victim->second.retry_at)
        victim = item;
    }
    if (victim == preview_entries_.end())
      return false;
    preview_entries_.erase(victim);
  }
  return true;
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

std::string ReadModelService::enqueue_preview(RefreshJob &job, std::chrono::steady_clock::time_point now) {
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
      if (existing->second.attempts >= preview_max_attempts &&
          now - existing->second.retry_at >= std::chrono::seconds(config_.preview_failure_ttl_seconds)) {
        existing->second.attempts = 0;
      } else if (now < existing->second.retry_at || existing->second.attempts >= preview_max_attempts) {
        return "failed";
      }
      break;
    }
    attempts = existing->second.attempts;
    drop_preview(job.key);
  }
  if (!trim_preview_entries(now) || !reserve_preview(job))
    return "saturated";
  preview_entries_[job.key].attempts = attempts;
  return {};
}

void ReadModelService::prepare_preview(nlohmann::json &item, const std::string &uuid, const ReadFence &fence) {
  if (!item.value("content", nlohmann::json(nullptr)).is_object())
    return;
  auto &content = item["content"];
  const auto file_id = content.value("preview_file_id", 0);
  const auto size = content.value("preview_size", std::int64_t{0});
  const auto mime = content.value("preview_mime", std::string{});
  content.erase("preview_file_id");
  content.erase("preview_size");
  content.erase("preview_mime");
  if (!reads::previewable(config_, file_id, size, mime))
    return;
  const auto chat_id = parse_int64(item.value("chat_id", std::string{"0"})).value_or(0);
  const auto message_id = reads::projection_id(item);
  content["preview_id"] = reads::preview_token(config_, uuid, fence, chat_id, message_id, file_id);
  auto job = refresh_job(RefreshKind::preview, uuid, fence, chat_id, "");
  job.key = reads::preview_key(uuid, fence, file_id);
  job.preview_file_id = file_id;
  job.preview_size = static_cast<std::size_t>(size);
  content["preview_state"] = enqueue_refresh(std::move(job));
}

nlohmann::json ReadModelService::preview(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id,
                                         const std::string &preview_id) {
  const auto opened = begin_read(uuid);
  if (!opened)
    return safe_error("authorization.invalid_state", 409);
  const auto fence = *opened;
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
  if (!reads::previewable(config_, file_id, size, mime) ||
      !token_matches(reads::preview_token(config_, uuid, fence, chat_id, message_id, file_id), preview_id))
    return safe_error("message.cache_miss", 404);
  const auto key = reads::preview_key(uuid, fence, file_id);
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
    auto *const account = fenced_account(uuid, fence);
    if (account == nullptr)
      return safe_error("sync.resync_required", 409);
    if (reads::changed_since(*account, fence, chat_id, message_id) != nullptr)
      return safe_error("message.cache_miss", 404);
  }
  {
    std::lock_guard lock(refresh_mutex_);
    if (const auto entry = preview_entries_.find(key); entry != preview_entries_.end())
      entry->second.used_at = std::chrono::steady_clock::now();
  }
  return {{"mime_type", mime}, {"bytes_base64", reads::base64_encode(bytes)}};
}

} // namespace telebezel::runtime
