#include "read_model_internal.hpp"
#include "telebezel/parse.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/status.hpp"
#include <algorithm>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace {
struct ChatBoundary {
  std::uint64_t version{0};
  std::int64_t order{0};
  std::int64_t id{0};
};

std::int64_t list_order(const nlohmann::json &item, const std::string &list) {
  return parse_int64(item["positions"][list].value("order", std::string{"0"})).value_or(0);
}

bool before_boundary(std::int64_t order, std::int64_t id, const ChatBoundary &boundary) {
  return order < boundary.order || (order == boundary.order && id < boundary.id);
}

bool disturbed(AccountState &account, const std::string &list, const std::optional<ChatBoundary> &boundary) {
  return boundary && order_log(account, list).disturbed(boundary->version, boundary->order);
}

bool &exhausted_flag(AccountState &account, const std::string &list) {
  return list == "archive" ? account.archive_exhausted : account.main_exhausted;
}
} // namespace

nlohmann::json ReadModelService::chats(const std::string &uuid, const std::string &list, std::size_t limit,
                                       const std::string &cursor) {
  const auto opened = begin_read(uuid);
  if (!opened)
    return safe_error("service.busy", 503);
  const auto &fence = *opened;
  std::optional<ChatBoundary> boundary;
  if (!cursor.empty()) {
    const auto dot = cursor.rfind('.');
    if (dot == std::string::npos)
      return safe_error("cursor.unusable", 409);
    const auto payload = cursor.substr(0, dot);
    const auto signature =
        reads::cursor_signature(config_, reads::chats_cursor_purpose, uuid, fence.generation, payload);
    const auto fields = reads::split_fields(payload, ':');
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
    boundary = ChatBoundary{*version, *order, *boundary_id};
    std::lock_guard lock(context_.mutex_);
    auto *const account = fenced_account(uuid, fence);
    if (account == nullptr || disturbed(*account, list, boundary))
      return safe_error("sync.resync_required", 409);
  }
  bool load_failed = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  for (int attempt = 0; attempt < 8 && std::chrono::steady_clock::now() < deadline; ++attempt) {
    {
      std::lock_guard lock(context_.mutex_);
      auto *const account = fenced_account(uuid, fence);
      if (account == nullptr || disturbed(*account, list, boundary))
        return safe_error("sync.resync_required", 409);
      std::size_t available = 0;
      for (const auto &[id, projection] : account->chats) {
        if (!projection.value("positions", nlohmann::json::object()).contains(list))
          continue;
        if (!boundary || before_boundary(list_order(projection, list), id, *boundary))
          ++available;
      }
      if (available > limit || exhausted_flag(*account, list))
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
    if (td_error_code(response, 404)) {
      std::lock_guard lock(context_.mutex_);
      if (auto *const account = fenced_account(uuid, fence))
        exhausted_flag(*account, list) = true;
      break;
    }
    if (!response || response->get_id() == td_api::error::ID) {
      load_failed = true;
      break;
    }
  }
  std::vector<nlohmann::json> items;
  std::uint64_t current_version = 0;
  bool exhausted = false;
  ReadEnvelope envelope;
  {
    std::lock_guard lock(context_.mutex_);
    auto *const account = fenced_account(uuid, fence);
    if (account == nullptr || disturbed(*account, list, boundary))
      return safe_error("sync.resync_required", 409);
    current_version = order_log(*account, list).version;
    for (const auto &[chat_id, projection] : account->chats) {
      static_cast<void>(chat_id);
      if (projection.value("positions", nlohmann::json::object()).contains(list))
        items.push_back(projection);
    }
    exhausted = exhausted_flag(*account, list);
    envelope = ReadEnvelope::capture(cursors_, *account, fence);
  }
  std::sort(items.begin(), items.end(), [&list](const auto &left, const auto &right) {
    const auto left_order = list_order(left, list);
    const auto right_order = list_order(right, list);
    return left_order == right_order ? reads::projection_id(left) > reads::projection_id(right)
                                     : left_order > right_order;
  });
  if (boundary) {
    items.erase(items.begin(), std::find_if(items.begin(), items.end(), [&list, &boundary](const auto &item) {
                  return before_boundary(list_order(item, list), reads::projection_id(item), *boundary);
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
    next_cursor =
        payload + "." + reads::cursor_signature(config_, reads::chats_cursor_purpose, uuid, fence.generation, payload);
    items.resize(limit);
  }
  {
    std::lock_guard lock(context_.mutex_);
    auto *const account = fenced_account(uuid, fence);
    if (account == nullptr || disturbed(*account, list, boundary))
      return safe_error("sync.resync_required", 409);
  }
  const bool partial = load_failed || (!truncated && !exhausted && items.size() < limit);
  return envelope.wrap(
      {{"items", items},
       {"partial", partial},
       {"local_exhausted", exhausted},
       {"has_more", truncated   ? nlohmann::json(true)
                    : exhausted ? nlohmann::json(false)
                                : nlohmann::json(nullptr)},
       {"next_cursor", next_cursor},
       {"retry_cursor",
        load_failed ? (cursor.empty() ? nlohmann::json(nullptr) : nlohmann::json(cursor)) : nlohmann::json(nullptr)},
       {"fallback_reason", load_failed ? nlohmann::json("load_failed") : nlohmann::json(nullptr)}},
      "tdlib_memory");
}

nlohmann::json ReadModelService::chat(const std::string &uuid, std::int64_t chat_id) const {
  std::lock_guard lock(context_.mutex_);
  const auto account = context_.accounts_.find(uuid);
  const auto fence = account == context_.accounts_.end() ? std::nullopt : read_fence(account->second);
  if (!fence)
    return safe_error("authorization.invalid_state", 409);
  const auto found = account->second.chats.find(chat_id);
  if (found == account->second.chats.end())
    return safe_error("chat.not_found", 404);
  auto item = found->second;
  if (item.value("last_message", nlohmann::json(nullptr)).is_object())
    decorate_sender(account->second, item["last_message"]);
  return ReadEnvelope::capture(cursors_, account->second, *fence)
      .wrap({{"item", item}, {"partial", false}}, "tdlib_memory");
}

} // namespace telebezel::runtime
