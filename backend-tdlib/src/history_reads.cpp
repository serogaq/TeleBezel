#include "read_model_internal.hpp"
#include "telebezel/parse.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/status.hpp"
#include <algorithm>
#include <td/telegram/td_api.h>
#include <unordered_set>
namespace telebezel::runtime {
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

nlohmann::json ReadModelService::messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit,
                                          const std::string &cursor) {
  const auto opened = begin_read(uuid);
  if (!opened)
    return safe_error("authorization.invalid_state", 409);
  const auto fence = *opened;
  std::int64_t anchor = 0;
  const std::string prefix =
      "h:" + std::to_string(fence.authorization_generation) + ":" + fence.epoch + ":" + std::to_string(chat_id) + ":";
  const auto make_cursor = [&](std::int64_t id) {
    const auto payload = prefix + std::to_string(id);
    return payload + "." +
           reads::cursor_signature(config_, reads::history_cursor_purpose, uuid, fence.generation, payload);
  };
  if (!cursor.empty()) {
    const auto dot = cursor.rfind('.');
    if (dot == std::string::npos || !cursor.starts_with(prefix))
      return safe_error("cursor.unusable", 409);
    const auto payload = cursor.substr(0, dot);
    const auto signature =
        reads::cursor_signature(config_, reads::history_cursor_purpose, uuid, fence.generation, payload);
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
    auto *const account = fenced_account(uuid, fence);
    if (account == nullptr)
      return safe_error("sync.resync_required", 409);
    for (auto &item : items) {
      decorate_sender(*account, item);
      const auto id = reads::projection_id(item);
      if (const auto *const changed = reads::changed_since(*account, fence, chat_id, id)) {
        if (changed->value("type", "") == "message_deleted")
          item = nullptr;
        else if (const auto current = account->messages.find({chat_id, id}); current != account->messages.end())
          item = current->second;
      } else {
        context_.put_message(*account, {chat_id, id}, item);
      }
    }
    items.erase(std::remove(items.begin(), items.end(), nullptr), items.end());
    std::sort(items.begin(), items.end(), [](const auto &left, const auto &right) {
      return reads::projection_id(left) > reads::projection_id(right);
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
      prepare_preview(item, uuid, fence);
    const auto last_id = items.empty() ? anchor : reads::projection_id(items.back());
    const bool progressed = !items.empty() && (anchor == 0 || last_id < anchor);
    // partial: the page could not be completed from local storage, unlike
    // local_exhausted, which says nothing older than the anchor exists locally.
    const bool partial = page.failed || (!full && !page.exhausted);
    result = ReadEnvelope::capture(cursors_, *account, fence)
                 .wrap({{"items", items},
                        {"partial", partial},
                        {"has_more", progressed                   ? nlohmann::json(true)
                                     : page.exhausted && !partial ? nlohmann::json(false)
                                                                  : nlohmann::json(nullptr)},
                        {"local_exhausted", page.exhausted},
                        {"next_cursor", progressed ? nlohmann::json(make_cursor(last_id)) : nlohmann::json(nullptr)},
                        {"retry_cursor", partial ? nlohmann::json(cursor.empty() ? make_cursor(anchor) : cursor)
                                                 : nlohmann::json(nullptr)},
                        {"refresh", "pending"},
                        {"fallback_reason", page.deadline ? nlohmann::json("read.deadline")
                                            : page.failed ? nlohmann::json("read.unavailable")
                                                          : nlohmann::json(nullptr)}},
                       "tdlib_local");
  }
  auto job = refresh_job(RefreshKind::history, uuid, fence, chat_id,
                         "history:" + std::to_string(chat_id) + ":" + std::to_string(anchor));
  job.anchor = anchor;
  job.limit = limit;
  result["refresh"] = enqueue_refresh(std::move(job));
  return result;
}

nlohmann::json ReadModelService::message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id) {
  const auto opened = begin_read(uuid);
  if (!opened)
    return safe_error("authorization.invalid_state", 409);
  const auto fence = *opened;
  const auto make_job = [&] {
    auto job = refresh_job(RefreshKind::message, uuid, fence, chat_id,
                           "message:" + std::to_string(chat_id) + ":" + std::to_string(message_id));
    job.message_id = message_id;
    job.limit = 1;
    return job;
  };
  const auto respond = [&](const AccountState &account, nlohmann::json item, const char *source,
                           const std::string &refresh) {
    decorate_sender(account, item);
    prepare_preview(item, uuid, fence);
    return ReadEnvelope::capture(cursors_, account, fence)
        .wrap({{"item", item}, {"partial", false}, {"refresh", refresh}, {"fallback_reason", nullptr}}, source);
  };
  {
    std::lock_guard lock(context_.mutex_);
    auto *const account = fenced_account(uuid, fence);
    if (account == nullptr)
      return safe_error("authorization.invalid_state", 409);
    if (const auto found = account->messages.find({chat_id, message_id}); found != account->messages.end())
      return respond(*account, found->second, "tdlib_memory", enqueue_refresh(make_job()));
  }
  const auto response = broker_.request(
      fence.client, td_api::make_object<td_api::getMessageLocally>(chat_id, message_id), std::chrono::seconds(2));
  const auto refresh = enqueue_refresh(make_job());
  std::lock_guard lock(context_.mutex_);
  auto *const account = fenced_account(uuid, fence);
  if (account == nullptr)
    return safe_error("sync.resync_required", 409);
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
  if (reads::changed_since(*account, fence, chat_id, message_id) != nullptr) {
    if (const auto current = account->messages.find({chat_id, message_id}); current != account->messages.end())
      return respond(*account, current->second, "tdlib_memory", refresh);
    return safe_error("sync.resync_required", 409);
  }
  decorate_sender(*account, projection);
  context_.put_message(*account, {chat_id, message_id}, projection);
  return respond(*account, std::move(projection), "tdlib_local", refresh);
}

} // namespace telebezel::runtime
