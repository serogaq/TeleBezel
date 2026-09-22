#include "read_model_internal.hpp"
#include "telebezel/crypto.hpp"
#include "telebezel/parse.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <openssl/evp.h>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace reads {
std::string preview_token(const Config &config, const std::string &uuid, const ReadFence &fence, std::int64_t chat_id,
                          std::int64_t message_id, std::int32_t file_id) {
  return hmac_sha256_hex(config.internal_token, preview_purpose,
                         uuid + ":" + fence.generation + ":" + fence.epoch + ":" +
                             std::to_string(fence.authorization_generation) + ":" + std::to_string(chat_id) + ":" +
                             std::to_string(message_id) + ":" + std::to_string(file_id));
}

std::string preview_key(const std::string &uuid, const ReadFence &fence, std::int32_t file_id) {
  return uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) +
         ":preview:" + std::to_string(file_id);
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

const nlohmann::json *changed_since(const AccountState &account, const ReadFence &fence, std::int64_t chat_id,
                                    std::int64_t message_id) {
  const auto chat = std::to_string(chat_id);
  const auto message = std::to_string(message_id);
  for (auto event = account.events.rbegin(); event != account.events.rend(); ++event) {
    if (event->value("sequence", 0ULL) <= fence.sequence)
      break;
    if (event->value("chat_id", "") == chat && event->value("message_id", "") == message)
      return &*event;
  }
  return nullptr;
}

bool previewable(const Config &config, std::int32_t file_id, std::int64_t size, const std::string &mime) {
  return file_id > 0 && size > 0 && static_cast<std::uint64_t>(size) <= config.preview_max_bytes &&
         static_cast<std::uint64_t>(size) <= config.preview_total_bytes &&
         (mime == "image/jpeg" || mime == "image/png" || mime == "image/webp");
}
} // namespace reads

namespace {
constexpr auto refresh_cooldown = std::chrono::seconds(5);
constexpr std::size_t refresh_queue_per_account = 32;
constexpr std::size_t refresh_results_retained = 512;
} // namespace

ReadEnvelope ReadEnvelope::capture(const CursorCodec &cursors, const AccountState &account, const ReadFence &fence) {
  return {cursors.encode(account, fence.sequence), account.connection_state,
          std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())};
}

nlohmann::json ReadEnvelope::wrap(nlohmann::json body, const char *source) const {
  body["stale"] = true;
  body["source"] = source;
  body["connection"] = connection;
  body["updates_cursor"] = updates_cursor;
  body["observed_at"] = observed_at;
  return body;
}

ReadModelService::~ReadModelService() { stop(); }

void ReadModelService::start() {
  std::lock_guard lock(refresh_mutex_);
  if (refresh_thread_.joinable())
    return;
  refresh_stopping_ = false;
  refresh_thread_ = std::thread(&ReadModelService::refresh_loop, this);
}

void ReadModelService::stop() {
  {
    std::lock_guard lock(refresh_mutex_);
    refresh_stopping_ = true;
  }
  refresh_condition_.notify_all();
  if (refresh_thread_.joinable())
    refresh_thread_.join();
}

bool ReadModelService::running() const {
  std::lock_guard lock(refresh_mutex_);
  return refresh_thread_.joinable() && !refresh_stopping_;
}

std::optional<ReadFence> ReadModelService::begin_read(const std::string &uuid) const {
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  return found == context_.accounts_.end() ? std::nullopt : read_fence(found->second);
}

AccountState *ReadModelService::fenced_account(const std::string &uuid, const ReadFence &fence) const {
  const auto found = context_.accounts_.find(uuid);
  return found == context_.accounts_.end() || !fence_valid(found->second, fence) ? nullptr : &found->second;
}

ReadModelService::RefreshJob ReadModelService::refresh_job(RefreshKind kind, const std::string &uuid,
                                                           const ReadFence &fence, std::int64_t chat_id,
                                                           const std::string &suffix) const {
  RefreshJob job;
  job.kind = kind;
  job.uuid = uuid;
  job.epoch = fence.epoch;
  job.authorization_generation = fence.authorization_generation;
  job.chat_id = chat_id;
  job.client = fence.client;
  job.key = uuid + ":" + fence.epoch + ":" + std::to_string(fence.authorization_generation) + ":" + suffix;
  return job;
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

std::string ReadModelService::enqueue_refresh(RefreshJob job) {
  std::lock_guard lock(refresh_mutex_);
  if (refresh_stopping_)
    return "stopped";
  if (refresh_keys_.contains(job.key))
    return "pending";
  const auto now = std::chrono::steady_clock::now();
  if (job.kind == RefreshKind::preview) {
    if (auto state = enqueue_preview(job, now); !state.empty())
      return state;
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

void ReadModelService::remember_outcome(const std::string &key, bool changed) {
  const auto now = std::chrono::steady_clock::now();
  if (refresh_results_.size() >= refresh_results_retained) {
    for (auto item = refresh_results_.begin(); item != refresh_results_.end();) {
      if (now - item->second.at >= refresh_cooldown)
        item = refresh_results_.erase(item);
      else
        ++item;
    }
  }
  while (refresh_results_.size() >= refresh_results_retained) {
    refresh_results_.erase(
        std::min_element(refresh_results_.begin(), refresh_results_.end(),
                         [](const auto &left, const auto &right) { return left.second.at < right.second.at; }));
  }
  refresh_results_[key] = {changed ? "completed" : "unchanged", now};
}

bool ReadModelService::publish_projections(const RefreshJob &job, const ReadFence &fence,
                                           const std::vector<nlohmann::json> &items) {
  bool changed = false;
  std::lock_guard lock(context_.mutex_);
  auto *const account = fenced_account(job.uuid, fence);
  if (account == nullptr)
    return false;
  for (const auto &item : items) {
    const auto id = reads::projection_id(item);
    if (reads::changed_since(*account, fence, job.chat_id, id) != nullptr)
      continue;
    auto decorated = item;
    decorate_sender(*account, decorated);
    const auto current = account->messages.find({job.chat_id, id});
    // Reading back the same projection is not a change; journalling it would
    // make every GET produce the event that triggers the next GET.
    if (current != account->messages.end() && current->second == decorated)
      continue;
    context_.put_message(*account, {job.chat_id, id}, std::move(decorated));
    UpdateJournal::append(context_, *account, "message_changed", job.chat_id, id);
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
    {
      std::unique_lock lock(refresh_mutex_);
      refresh_condition_.wait(lock, [this] { return refresh_stopping_ || !refresh_rotation_.empty(); });
      if (refresh_stopping_)
        return;
    }
    guarded_iteration("read_refresh", [this] { refresh_once(); });
  }
}

void ReadModelService::refresh_once() {
  RefreshJob job;
  {
    std::lock_guard lock(refresh_mutex_);
    if (refresh_rotation_.empty())
      return;
    const auto uuid = refresh_rotation_.front();
    refresh_rotation_.pop_front();
    const auto queue = refresh_queues_.find(uuid);
    if (queue == refresh_queues_.end())
      return;
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
    if (job.kind == RefreshKind::history || job.kind == RefreshKind::message)
      remember_outcome(job.key, changed);
  }
  context_.condition_.notify_all();
}

nlohmann::json ReadModelService::updates(const std::string &uuid, const std::string &cursor, std::size_t limit,
                                         std::chrono::seconds wait) const {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::min<std::chrono::seconds>(wait, std::chrono::seconds(config_.updates_wait_max_seconds));
  std::unique_lock lock(context_.mutex_);
  while (true) {
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
    if (sequence >= account.event_sequence && std::chrono::steady_clock::now() < deadline) {
      context_.condition_.wait_until(lock, deadline);
      continue;
    }
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
}

} // namespace telebezel::runtime
