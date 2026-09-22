#include "telebezel/crypto.hpp"
#include "telebezel/parse.hpp"
#include "telebezel/runtime/support.hpp"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {

void report_thread_failure(const char *thread, const char *error) noexcept {
  try {
    static std::mutex log_mutex;
    std::string detail(error == nullptr ? "unknown" : error);
    if (detail.size() > 160)
      detail.resize(160);
    const auto line =
        nlohmann::json{{"event", "background_iteration_failed"}, {"thread", thread}, {"error", detail}}.dump(
            -1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::lock_guard lock(log_mutex);
    std::cerr << line << '\n';
  } catch (...) {
  }
}

std::optional<ReadFence> read_fence(const AccountState &account) {
  if (!account.reconciled || account.closed || account.tombstone || account.lifecycle != "active" ||
      account.authorization_state != "ready" || account.client_id == 0 || account.generation_unpersisted)
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

bool td_error_code(const td_api::object_ptr<td_api::Object> &object, int code) {
  return object && object->get_id() == td_api::error::ID && static_cast<const td_api::error &>(*object).code_ == code;
}
bool td_error_message(const td_api::object_ptr<td_api::Object> &object, const std::string &message) {
  return object && object->get_id() == td_api::error::ID &&
         static_cast<const td_api::error &>(*object).message_ == message;
}
void throw_runtime_control_error(const td_api::object_ptr<td_api::Object> &object) {
  if (td_error_code(object, 504))
    throw std::runtime_error("operation.outcome_unknown");
  if (td_error_message(object, "service.busy"))
    throw std::runtime_error("service.busy");
  if (td_error_message(object, "service.stopping"))
    throw std::runtime_error("service.stopping");
}
std::string nullable_string(const nlohmann::json &value, const char *key) {
  const auto iterator = value.find(key);
  return iterator != value.end() && iterator->is_string() ? iterator->get<std::string>() : std::string{};
}
std::string command_fingerprint(nlohmann::json command) {
  command.erase("mode");
  command.erase("lifecycle");
  command.erase("operation_id");
  command.erase("authorization_generation");
  // The whole proxy configuration is part of the identity of an intent: a
  // payload carrying only the profile id is a different, truncated intent. The
  // prefix names that rule, so a manifest written under the previous rule is
  // recognised as such instead of conflicting for ever.
  return std::string(fingerprint_version) + sha256_hex(command.dump());
}
std::pair<std::string, bool> delivery_method(const td_api::AuthenticationCodeType *type) {
  if (type == nullptr)
    return {"unknown", false};
  if (type->get_id() == td_api::authenticationCodeTypeTelegramMessage::ID)
    return {"telegram_message", true};
  if (type->get_id() == td_api::authenticationCodeTypeSms::ID)
    return {"sms", true};
  if (type->get_id() == td_api::authenticationCodeTypeSmsWord::ID)
    return {"sms_word", true};
  if (type->get_id() == td_api::authenticationCodeTypeSmsPhrase::ID)
    return {"sms_phrase", true};
  if (type->get_id() == td_api::authenticationCodeTypeCall::ID)
    return {"call", true};
  if (type->get_id() == td_api::authenticationCodeTypeFlashCall::ID)
    return {"flash_call", true};
  if (type->get_id() == td_api::authenticationCodeTypeMissedCall::ID)
    return {"missed_call", true};
  if (type->get_id() == td_api::authenticationCodeTypeFragment::ID)
    return {"fragment", true};
  if (type->get_id() == td_api::authenticationCodeTypeFirebaseAndroid::ID ||
      type->get_id() == td_api::authenticationCodeTypeFirebaseIos::ID)
    return {"firebase", false};
  return {"unknown", false};
}
nlohmann::json message_content(const td_api::MessageContent *content) {
  if (content != nullptr && content->get_id() == td_api::messageText::ID) {
    const auto &text = static_cast<const td_api::messageText &>(*content);
    return {{"kind", "text"}, {"text", text.text_ ? text.text_->text_ : std::string{}}};
  }
  if (content != nullptr && content->get_id() == td_api::messagePhoto::ID) {
    const auto &photo = static_cast<const td_api::messagePhoto &>(*content);
    if (photo.is_secret_)
      return {{"kind", "photo"}, {"fallback_key", "message.photo"}};
    const td_api::file *candidate = nullptr;
    if (photo.photo_)
      for (const auto &size : photo.photo_->sizes_)
        if (size && size->photo_ && size->photo_->size_ > 0 &&
            (candidate == nullptr || size->photo_->size_ < candidate->size_))
          candidate = size->photo_.get();
    if (candidate)
      return {{"kind", "photo"},
              {"fallback_key", "message.photo"},
              {"preview_file_id", candidate->id_},
              {"preview_size", candidate->size_},
              {"preview_mime", "image/jpeg"}};
    return {{"kind", "photo"}, {"fallback_key", "message.photo"}};
  }
  if (content != nullptr && content->get_id() == td_api::messageVideo::ID) {
    const auto &video = static_cast<const td_api::messageVideo &>(*content);
    const auto *thumbnail = video.video_ && video.video_->thumbnail_ ? video.video_->thumbnail_.get() : nullptr;
    if (thumbnail && thumbnail->file_ && thumbnail->format_ && thumbnail->file_->size_ > 0) {
      const auto format = thumbnail->format_->get_id();
      const auto mime = format == td_api::thumbnailFormatJpeg::ID   ? "image/jpeg"
                        : format == td_api::thumbnailFormatPng::ID  ? "image/png"
                        : format == td_api::thumbnailFormatWebp::ID ? "image/webp"
                                                                    : "";
      if (*mime != '\0')
        return {{"kind", "video"},
                {"fallback_key", "message.video"},
                {"preview_file_id", thumbnail->file_->id_},
                {"preview_size", thumbnail->file_->size_},
                {"preview_mime", mime}};
    }
    return {{"kind", "video"}, {"fallback_key", "message.video"}};
  }
  return {{"kind", "unsupported"}, {"fallback_key", "message.unsupported"}};
}
nlohmann::json message_projection(const td_api::message &message) {
  nlohmann::json sender = nullptr;
  if (message.sender_id_) {
    if (message.sender_id_->get_id() == td_api::messageSenderUser::ID)
      sender = {{"type", "user"},
                {"id", std::to_string(static_cast<const td_api::messageSenderUser &>(*message.sender_id_).user_id_)}};
    else if (message.sender_id_->get_id() == td_api::messageSenderChat::ID)
      sender = {{"type", "chat"},
                {"id", std::to_string(static_cast<const td_api::messageSenderChat &>(*message.sender_id_).chat_id_)}};
  }
  if (sender.is_object()) {
    const auto type = sender.value("type", "user");
    const auto id = sender.value("id", "0");
    sender["name"] = nullptr;
    sender["fallback"] = (type == "chat" ? "Chat " : "User ") + id;
  }
  return {{"id", std::to_string(message.id_)},
          {"chat_id", std::to_string(message.chat_id_)},
          {"sender", sender},
          {"date", message.date_},
          {"edit_date", message.edit_date_},
          {"is_outgoing", message.is_outgoing_},
          {"author_signature", message.author_signature_},
          {"content", message_content(message.content_.get())}};
}
void decorate_sender(const Account &account, nlohmann::json &message) {
  if (!message.is_object() || !message.value("sender", nlohmann::json(nullptr)).is_object())
    return;
  auto &sender = message["sender"];
  const auto key = sender.value("type", "") + ":" + sender.value("id", "");
  if (const auto found = account.sender_names.find(key); found != account.sender_names.end())
    sender["name"] = found->second;
}
std::string chat_type(const td_api::ChatType *type) {
  if (type == nullptr)
    return "unknown";
  if (type->get_id() == td_api::chatTypePrivate::ID)
    return "private";
  if (type->get_id() == td_api::chatTypeBasicGroup::ID)
    return "basic_group";
  if (type->get_id() == td_api::chatTypeSupergroup::ID)
    return static_cast<const td_api::chatTypeSupergroup &>(*type).is_channel_ ? "channel" : "supergroup";
  if (type->get_id() == td_api::chatTypeSecret::ID)
    return "secret";
  return "unknown";
}
void apply_position(nlohmann::json &projection, const td_api::chatPosition *position) {
  if (position == nullptr || position->list_ == nullptr)
    return;
  const std::string list = position->list_->get_id() == td_api::chatListMain::ID      ? "main"
                           : position->list_->get_id() == td_api::chatListArchive::ID ? "archive"
                                                                                      : "other";
  if (position->order_ == 0)
    projection["positions"].erase(list);
  else
    projection["positions"][list] = {{"order", std::to_string(position->order_)}, {"is_pinned", position->is_pinned_}};
}
nlohmann::json chat_projection(const td_api::chat &chat) {
  nlohmann::json projection{
      {"id", std::to_string(chat.id_)},
      {"type", chat_type(chat.type_.get())},
      {"title", chat.title_},
      {"is_forum", chat.view_as_topics_},
      {"is_marked_unread", chat.is_marked_as_unread_},
      {"unread_count", chat.unread_count_},
      {"last_read_inbox_message_id", std::to_string(chat.last_read_inbox_message_id_)},
      {"last_read_outbox_message_id", std::to_string(chat.last_read_outbox_message_id_)},
      {"positions", nlohmann::json::object()},
      {"unread_mention_count", chat.unread_mention_count_},
      {"unread_reaction_count", chat.unread_reaction_count_},
      {"notifications", notification_projection(chat.notification_settings_.get())},
      {"last_message", chat.last_message_ ? message_projection(*chat.last_message_) : nlohmann::json(nullptr)}};
  for (const auto &position : chat.positions_)
    apply_position(projection, position.get());
  return projection;
}

nlohmann::json notification_projection(const td_api::chatNotificationSettings *settings) {
  if (settings == nullptr)
    return {{"use_default_mute_for", true}, {"mute_for", 0}};
  return {{"use_default_mute_for", settings->use_default_mute_for_}, {"mute_for", settings->mute_for_}};
}

nlohmann::json *existing_chat(Account &account, std::int64_t chat_id) {
  const auto found = account.chats.find(chat_id);
  return found == account.chats.end() ? nullptr : &found->second;
}

ChatOrderLog &order_log(Account &account, const std::string &list) {
  return list == "archive" ? account.archive_orders : account.main_orders;
}

void record_order_change(Account &account, const nlohmann::json &before, const nlohmann::json &after) {
  for (const auto *list : {"main", "archive"}) {
    const auto previous = before.is_object() ? before.value(list, nlohmann::json::object()) : nlohmann::json::object();
    const auto current = after.is_object() ? after.value(list, nlohmann::json::object()) : nlohmann::json::object();
    if (previous == current)
      continue;
    std::int64_t order = 0;
    for (const auto &position : {previous, current})
      if (const auto parsed = parse_int64(position.value("order", std::string{"0"})))
        order = std::max(order, *parsed);
    order_log(account, list).record(order);
  }
}

std::string authorization_name(std::int32_t id) {
  if (id == td_api::authorizationStateWaitTdlibParameters::ID)
    return "initializing";
  if (id == td_api::authorizationStateWaitPhoneNumber::ID)
    return "awaiting_phone_number";
  if (id == td_api::authorizationStateWaitPremiumPurchase::ID)
    return "premium_purchase_required";
  if (id == td_api::authorizationStateWaitEmailAddress::ID)
    return "awaiting_email_address";
  if (id == td_api::authorizationStateWaitEmailCode::ID)
    return "awaiting_email_code";
  if (id == td_api::authorizationStateWaitCode::ID)
    return "awaiting_code";
  if (id == td_api::authorizationStateWaitOtherDeviceConfirmation::ID)
    return "awaiting_qr_confirmation";
  if (id == td_api::authorizationStateWaitRegistration::ID)
    return "registration_required";
  if (id == td_api::authorizationStateWaitPassword::ID)
    return "awaiting_password";
  if (id == td_api::authorizationStateReady::ID)
    return "ready";
  if (id == td_api::authorizationStateLoggingOut::ID)
    return "logging_out";
  if (id == td_api::authorizationStateClosing::ID)
    return "closing";
  if (id == td_api::authorizationStateClosed::ID)
    return "closed";
  return "error";
}

std::string connection_name(std::int32_t id) {
  if (id == td_api::connectionStateWaitingForNetwork::ID)
    return "waiting_for_network";
  if (id == td_api::connectionStateConnectingToProxy::ID)
    return "connecting_to_proxy";
  if (id == td_api::connectionStateConnecting::ID)
    return "connecting";
  if (id == td_api::connectionStateUpdating::ID)
    return "updating";
  if (id == td_api::connectionStateReady::ID)
    return "ready";
  return "unknown";
}

std::vector<std::string> allowed_actions(const Account &account) {
  const auto &state = account.authorization_state;
  if (state == "awaiting_phone_number")
    return {"submit_phone_number", "start_qr"};
  if (state == "awaiting_code") {
    std::vector<std::string> actions{"submit_code", "submit_phone_number", "start_qr"};
    if (account.delivery_supported && std::chrono::steady_clock::now() >= account.resend_available_at)
      actions.push_back("resend_code");
    return actions;
  }
  if (state == "awaiting_password")
    return {"submit_password"};
  if (state == "awaiting_email_address")
    return {"submit_email_address"};
  if (state == "awaiting_email_code")
    return {"submit_email_code", "resend_code"};
  if (state == "awaiting_qr_confirmation")
    return {"start_qr", "submit_phone_number"};
  return {};
}

nlohmann::json safe_error(const std::string &code, int status) {
  return {{"_error", true}, {"code", code}, {"status", status}};
}

nlohmann::json safe_error(const std::string &code, int status, std::int64_t retry_after) {
  auto error = safe_error(code, status);
  error["retry_after"] = std::max<std::int64_t>(1, retry_after);
  return error;
}

std::optional<std::int64_t> flood_wait_seconds(const std::string &message) {
  for (const std::string marker : {"FLOOD_WAIT_", "retry after "}) {
    const auto found = message.find(marker);
    if (found == std::string::npos)
      continue;
    const auto start = found + marker.size();
    auto end = start;
    while (end < message.size() && message[end] >= '0' && message[end] <= '9')
      ++end;
    if (const auto seconds = parse_int64(std::string_view(message).substr(start, end - start)); seconds && *seconds > 0)
      return std::min<std::int64_t>(*seconds, 86400);
  }
  return std::nullopt;
}

nlohmann::json account_json(const Account &account) {
  const auto resend_after =
      account.resend_available_at == std::chrono::steady_clock::time_point{}
          ? 0
          : std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::seconds>(account.resend_available_at -
                                                                                       std::chrono::steady_clock::now())
                                          .count());
  const bool resend_state =
      account.authorization_state == "awaiting_code" || account.authorization_state == "awaiting_email_code";
  nlohmann::json authorization{{"state", account.authorization_state},
                               {"authorization_version", account.authorization_version},
                               {"allowed_actions", allowed_actions(account)},
                               {"resend_available", resend_state && account.delivery_supported && resend_after == 0},
                               {"resend_after_seconds", resend_after}};
  if (!account.delivery_method.empty()) {
    authorization["delivery_method"] = account.delivery_method;
  }
  if (account.authorization_state == "awaiting_qr_confirmation" && !account.qr_link.empty()) {
    authorization["qr_link"] = account.qr_link;
  }
  nlohmann::json result{
      {"uuid", account.uuid},
      {"generation", account.generation},
      {"target_revision", account.revision},
      {"applied_revision", account.applied_revision},
      {"authorization_generation", account.authorization_generation},
      {"effective_config_id",
       account.effective_config_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(account.effective_config_id)},
      {"runtime_available", account.reconciled && !account.closed},
      {"authorization_state", account.authorization_state},
      {"connection_state", account.connection_state},
      {"authorization", authorization},
      {"operation_id", account.operation_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(account.operation_id)},
      {"last_error_code", account.last_error.empty() ? nlohmann::json(nullptr) : nlohmann::json(account.last_error)}};
  if (!account.telegram_identity.is_null()) {
    result["telegram_identity"] = account.telegram_identity;
  }
  return result;
}

} // namespace telebezel::runtime
