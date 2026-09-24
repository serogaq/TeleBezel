#include "telebezel/crypto.hpp"
#include "telebezel/parse.hpp"
#include "telebezel/runtime/support.hpp"
#include <algorithm>
#include <cstdio>
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
    std::fputs("{\"event\":\"background_iteration_failed\"}\n", stderr);
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
namespace {
void set_caption(nlohmann::json &result, const td_api::object_ptr<td_api::formattedText> &caption) {
  if (caption && !caption->text_.empty())
    result["text"] = caption->text_;
}
void set_string(nlohmann::json &result, const char *key, const std::string &value) {
  if (!value.empty())
    result[key] = value;
}
nlohmann::json content_kind(const char *kind) {
  return {{"kind", kind}, {"fallback_key", std::string("message.") + kind}};
}
nlohmann::json service_content(const char *action) {
  auto result = content_kind("service");
  result["action"] = action;
  return result;
}
nlohmann::json photo_content(const td_api::messagePhoto &photo) {
  auto result = content_kind("photo");
  set_caption(result, photo.caption_);
  if (photo.is_secret_)
    return result;
  const td_api::file *candidate = nullptr;
  if (photo.photo_)
    for (const auto &size : photo.photo_->sizes_)
      if (size && size->photo_ && size->photo_->size_ > 0 &&
          (candidate == nullptr || size->photo_->size_ < candidate->size_))
        candidate = size->photo_.get();
  if (candidate) {
    result["preview_file_id"] = candidate->id_;
    result["preview_size"] = candidate->size_;
    result["preview_mime"] = "image/jpeg";
  }
  return result;
}
nlohmann::json video_content(const td_api::messageVideo &video) {
  auto result = content_kind("video");
  set_caption(result, video.caption_);
  if (video.video_)
    result["duration"] = video.video_->duration_;
  if (video.is_secret_)
    return result;
  const auto *thumbnail = video.video_ && video.video_->thumbnail_ ? video.video_->thumbnail_.get() : nullptr;
  if (thumbnail && thumbnail->file_ && thumbnail->format_ && thumbnail->file_->size_ > 0) {
    const auto format = thumbnail->format_->get_id();
    const auto mime = format == td_api::thumbnailFormatJpeg::ID   ? "image/jpeg"
                      : format == td_api::thumbnailFormatPng::ID  ? "image/png"
                      : format == td_api::thumbnailFormatWebp::ID ? "image/webp"
                                                                  : "";
    if (*mime != '\0') {
      result["preview_file_id"] = thumbnail->file_->id_;
      result["preview_size"] = thumbnail->file_->size_;
      result["preview_mime"] = mime;
    }
  }
  return result;
}
std::optional<nlohmann::json> media_content(const td_api::MessageContent &content) {
  switch (content.get_id()) {
  case td_api::messagePhoto::ID:
    return photo_content(static_cast<const td_api::messagePhoto &>(content));
  case td_api::messageVideo::ID:
    return video_content(static_cast<const td_api::messageVideo &>(content));
  case td_api::messageVoiceNote::ID: {
    const auto &voice = static_cast<const td_api::messageVoiceNote &>(content);
    auto result = content_kind("voice_note");
    set_caption(result, voice.caption_);
    if (voice.voice_note_)
      result["duration"] = voice.voice_note_->duration_;
    return result;
  }
  case td_api::messageVideoNote::ID: {
    const auto &note = static_cast<const td_api::messageVideoNote &>(content);
    auto result = content_kind("video_note");
    if (note.video_note_)
      result["duration"] = note.video_note_->duration_;
    return result;
  }
  case td_api::messageSticker::ID: {
    const auto &sticker = static_cast<const td_api::messageSticker &>(content);
    auto result = content_kind("sticker");
    if (sticker.sticker_)
      set_string(result, "emoji", sticker.sticker_->emoji_);
    return result;
  }
  case td_api::messageDocument::ID: {
    const auto &document = static_cast<const td_api::messageDocument &>(content);
    auto result = content_kind("document");
    set_caption(result, document.caption_);
    if (document.document_)
      set_string(result, "title", document.document_->file_name_);
    return result;
  }
  case td_api::messageAudio::ID: {
    const auto &audio = static_cast<const td_api::messageAudio &>(content);
    auto result = content_kind("audio");
    set_caption(result, audio.caption_);
    if (audio.audio_) {
      result["duration"] = audio.audio_->duration_;
      const auto &performer = audio.audio_->performer_;
      const auto &title = audio.audio_->title_;
      set_string(result, "title",
                 !performer.empty() && !title.empty() ? performer + " - " + title
                 : !title.empty()                     ? title
                 : !performer.empty()                 ? performer
                                                      : audio.audio_->file_name_);
    }
    return result;
  }
  case td_api::messageAnimation::ID: {
    const auto &animation = static_cast<const td_api::messageAnimation &>(content);
    auto result = content_kind("animation");
    set_caption(result, animation.caption_);
    return result;
  }
  case td_api::messagePaidMedia::ID: {
    const auto &paid = static_cast<const td_api::messagePaidMedia &>(content);
    auto result = content_kind("paid_media");
    set_caption(result, paid.caption_);
    return result;
  }
  default:
    return std::nullopt;
  }
}
std::optional<nlohmann::json> shared_content(const td_api::MessageContent &content) {
  switch (content.get_id()) {
  case td_api::messageLocation::ID:
  case td_api::messageLiveLocation::ID:
    return content_kind("location");
  case td_api::messageVenue::ID: {
    const auto &venue = static_cast<const td_api::messageVenue &>(content);
    auto result = content_kind("venue");
    if (venue.venue_) {
      set_string(result, "title", venue.venue_->title_);
      set_string(result, "text", venue.venue_->address_);
    }
    return result;
  }
  case td_api::messageContact::ID: {
    const auto &contact = static_cast<const td_api::messageContact &>(content);
    auto result = content_kind("contact");
    if (contact.contact_) {
      const auto &first = contact.contact_->first_name_;
      const auto &last = contact.contact_->last_name_;
      set_string(result, "title", first.empty() || last.empty() ? first + last : first + " " + last);
    }
    return result;
  }
  case td_api::messagePoll::ID: {
    const auto &poll = static_cast<const td_api::messagePoll &>(content);
    auto result = content_kind("poll");
    if (poll.poll_)
      set_caption(result, poll.poll_->question_);
    return result;
  }
  case td_api::messageDice::ID: {
    const auto &dice = static_cast<const td_api::messageDice &>(content);
    auto result = content_kind("dice");
    set_string(result, "emoji", dice.emoji_);
    return result;
  }
  case td_api::messageAnimatedEmoji::ID: {
    const auto &emoji = static_cast<const td_api::messageAnimatedEmoji &>(content);
    nlohmann::json result{{"kind", "text"}};
    result["text"] = emoji.emoji_;
    return result;
  }
  case td_api::messageCall::ID: {
    const auto &call = static_cast<const td_api::messageCall &>(content);
    auto result = content_kind("call");
    result["duration"] = call.duration_;
    return result;
  }
  case td_api::messageGame::ID: {
    const auto &game = static_cast<const td_api::messageGame &>(content);
    auto result = content_kind("game");
    if (game.game_)
      set_string(result, "title", game.game_->title_);
    return result;
  }
  case td_api::messageStory::ID:
    return content_kind("story");
  case td_api::messageExpiredPhoto::ID:
  case td_api::messageExpiredVideo::ID:
  case td_api::messageExpiredVideoNote::ID:
  case td_api::messageExpiredVoiceNote::ID:
    return content_kind("expired");
  default:
    return std::nullopt;
  }
}
std::optional<nlohmann::json> service_message(const td_api::MessageContent &content) {
  switch (content.get_id()) {
  case td_api::messageChatAddMembers::ID:
    return service_content("members_added");
  case td_api::messageChatJoinByLink::ID:
  case td_api::messageChatJoinByRequest::ID:
    return service_content("member_joined");
  case td_api::messageChatDeleteMember::ID:
    return service_content("member_left");
  case td_api::messageChatChangeTitle::ID: {
    auto result = service_content("title_changed");
    set_string(result, "title", static_cast<const td_api::messageChatChangeTitle &>(content).title_);
    return result;
  }
  case td_api::messageChatChangePhoto::ID:
  case td_api::messageChatDeletePhoto::ID:
    return service_content("photo_changed");
  case td_api::messageBasicGroupChatCreate::ID: {
    auto result = service_content("chat_created");
    set_string(result, "title", static_cast<const td_api::messageBasicGroupChatCreate &>(content).title_);
    return result;
  }
  case td_api::messageSupergroupChatCreate::ID: {
    auto result = service_content("chat_created");
    set_string(result, "title", static_cast<const td_api::messageSupergroupChatCreate &>(content).title_);
    return result;
  }
  case td_api::messagePinMessage::ID:
    return service_content("pinned");
  case td_api::messageScreenshotTaken::ID:
    return service_content("screenshot");
  case td_api::messageContactRegistered::ID:
    return service_content("contact_joined");
  case td_api::messageVideoChatScheduled::ID:
  case td_api::messageVideoChatStarted::ID:
  case td_api::messageVideoChatEnded::ID:
  case td_api::messageGroupCall::ID:
    return service_content("video_chat");
  case td_api::messageChatSetMessageAutoDeleteTime::ID:
    return service_content("auto_delete");
  case td_api::messageChatUpgradeTo::ID:
  case td_api::messageChatUpgradeFrom::ID:
    return service_content("upgraded");
  case td_api::messageForumTopicCreated::ID: {
    auto result = service_content("topic_created");
    set_string(result, "title", static_cast<const td_api::messageForumTopicCreated &>(content).name_);
    return result;
  }
  case td_api::messageCustomServiceAction::ID: {
    auto result = service_content("custom");
    set_string(result, "text", static_cast<const td_api::messageCustomServiceAction &>(content).text_);
    return result;
  }
  default:
    return std::nullopt;
  }
}
} // namespace
nlohmann::json message_content(const td_api::MessageContent *content) {
  if (content == nullptr)
    return content_kind("unsupported");
  if (content->get_id() == td_api::messageText::ID) {
    const auto &text = static_cast<const td_api::messageText &>(*content);
    return {{"kind", "text"}, {"text", text.text_ ? text.text_->text_ : std::string{}}};
  }
  if (auto media = media_content(*content))
    return std::move(*media);
  if (auto shared = shared_content(*content))
    return std::move(*shared);
  if (auto service = service_message(*content))
    return std::move(*service);
  return content_kind("unsupported");
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
  nlohmann::json forward_from = nullptr;
  const auto party = [](const char *type, std::int64_t id, const std::string &signature) {
    nlohmann::json value{
        {"type", type},
        {"id", std::to_string(id)},
        {"name", nullptr},
        {"fallback", std::string(std::string(type) == "chat" ? "Chat " : "User ") + std::to_string(id)}};
    if (!signature.empty())
      value["signature"] = signature;
    return value;
  };
  if (message.forward_info_ && message.forward_info_->origin_) {
    const auto &origin = *message.forward_info_->origin_;
    if (origin.get_id() == td_api::messageOriginUser::ID) {
      forward_from = party("user", static_cast<const td_api::messageOriginUser &>(origin).sender_user_id_, {});
    } else if (origin.get_id() == td_api::messageOriginHiddenUser::ID) {
      const auto &hidden = static_cast<const td_api::messageOriginHiddenUser &>(origin);
      forward_from = {{"type", "hidden"}, {"name", hidden.sender_name_}, {"fallback", hidden.sender_name_}};
    } else if (origin.get_id() == td_api::messageOriginChat::ID) {
      const auto &chat = static_cast<const td_api::messageOriginChat &>(origin);
      forward_from = party("chat", chat.sender_chat_id_, chat.author_signature_);
    } else if (origin.get_id() == td_api::messageOriginChannel::ID) {
      const auto &channel = static_cast<const td_api::messageOriginChannel &>(origin);
      forward_from = party("chat", channel.chat_id_, channel.author_signature_);
    }
  } else if (message.import_info_ && !message.import_info_->sender_name_.empty()) {
    forward_from = {{"type", "hidden"},
                    {"name", message.import_info_->sender_name_},
                    {"fallback", message.import_info_->sender_name_}};
  }
  nlohmann::json reply_to = nullptr;
  if (message.reply_to_ && message.reply_to_->get_id() == td_api::messageReplyToMessage::ID) {
    const auto &reply = static_cast<const td_api::messageReplyToMessage &>(*message.reply_to_);
    if (reply.message_id_ != 0 && (reply.chat_id_ == 0 || reply.chat_id_ == message.chat_id_))
      reply_to = {{"message_id", std::to_string(reply.message_id_)}};
  }
  return {{"id", std::to_string(message.id_)},
          {"chat_id", std::to_string(message.chat_id_)},
          {"sender", sender},
          {"date", message.date_},
          {"edit_date", message.edit_date_},
          {"is_outgoing", message.is_outgoing_},
          {"author_signature", message.author_signature_},
          {"forward_from", forward_from},
          {"reply_to", reply_to},
          {"sending_state", sending_state_name(message.sending_state_.get())},
          {"content", message_content(message.content_.get())}};
}
nlohmann::json sending_state_name(const td_api::MessageSendingState *state) {
  if (state == nullptr)
    return nullptr;
  if (state->get_id() == td_api::messageSendingStatePending::ID)
    return "pending";
  if (state->get_id() == td_api::messageSendingStateFailed::ID)
    return "failed";
  return nullptr;
}
std::string utf8_prefix(const std::string &text, std::size_t bytes) {
  if (text.size() <= bytes)
    return text;
  auto end = bytes;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U)
    --end;
  return text.substr(0, end);
}
namespace {
void decorate_name(const Account &account, nlohmann::json &sender) {
  const auto key = sender.value("type", "") + ":" + sender.value("id", "");
  if (const auto found = account.sender_names.find(key); found != account.sender_names.end())
    sender["name"] = found->second;
}
} // namespace
void decorate_sender(const Account &account, nlohmann::json &message) {
  if (!message.is_object())
    return;
  if (message.value("sender", nlohmann::json(nullptr)).is_object())
    decorate_name(account, message["sender"]);
  if (auto forward = message.find("forward_from");
      forward != message.end() && forward->is_object() && forward->contains("id"))
    decorate_name(account, *forward);
  auto reply = message.find("reply_to");
  if (reply == message.end() || !reply->is_object())
    return;
  const auto chat = parse_int64(message.value("chat_id", std::string{"0"}));
  const auto target = parse_int64(reply->value("message_id", std::string{"0"}));
  if (!chat || !target)
    return;
  const auto original = account.messages.find({*chat, *target});
  if (original == account.messages.end())
    return;
  auto author = original->second.value("forward_from", nlohmann::json(nullptr));
  if (!author.is_object())
    author = original->second.value("sender", nlohmann::json(nullptr));
  if (author.is_object()) {
    if (author.contains("id"))
      decorate_name(account, author);
    const auto name = author.value("name", nlohmann::json(nullptr));
    (*reply)["sender_name"] = name.is_string() ? name : author.value("fallback", nlohmann::json(nullptr));
  }
  const auto content = original->second.value("content", nlohmann::json::object());
  if (content.value("text", nlohmann::json(nullptr)).is_string())
    (*reply)["text"] = utf8_prefix(content.value("text", std::string{}), 100);
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
      {"is_saved_messages", false},
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

SendContext send_context(const td_api::chat &chat) {
  SendContext context;
  context.basic_allowed = !chat.permissions_ || chat.permissions_->can_send_basic_messages_;
  context.kind = "unknown";
  const auto *type = chat.type_.get();
  if (type == nullptr)
    return context;
  if (type->get_id() == td_api::chatTypePrivate::ID) {
    context.kind = "private";
    context.peer = static_cast<const td_api::chatTypePrivate &>(*type).user_id_;
  } else if (type->get_id() == td_api::chatTypeSecret::ID) {
    context.kind = "secret";
    context.peer = static_cast<const td_api::chatTypeSecret &>(*type).user_id_;
  } else if (type->get_id() == td_api::chatTypeBasicGroup::ID) {
    context.kind = "basic";
    context.peer = static_cast<const td_api::chatTypeBasicGroup &>(*type).basic_group_id_;
  } else if (type->get_id() == td_api::chatTypeSupergroup::ID) {
    const auto &supergroup = static_cast<const td_api::chatTypeSupergroup &>(*type);
    context.kind = supergroup.is_channel_ ? "channel" : "super";
    context.peer = supergroup.supergroup_id_;
  }
  return context;
}

MemberStatus member_status(const td_api::ChatMemberStatus *status, bool channel) {
  if (status == nullptr)
    return {};
  switch (status->get_id()) {
  case td_api::chatMemberStatusCreator::ID:
    return {true, ""};
  case td_api::chatMemberStatusAdministrator::ID: {
    const auto &admin = static_cast<const td_api::chatMemberStatusAdministrator &>(*status);
    if (!channel || (admin.rights_ && admin.rights_->can_post_messages_))
      return {true, ""};
    return {false, "read_only"};
  }
  case td_api::chatMemberStatusMember::ID:
    return channel ? MemberStatus{false, "read_only"} : MemberStatus{};
  case td_api::chatMemberStatusRestricted::ID: {
    const auto &restricted = static_cast<const td_api::chatMemberStatusRestricted &>(*status);
    if (!restricted.is_member_)
      return {false, "not_member"};
    if (restricted.permissions_ && !restricted.permissions_->can_send_basic_messages_)
      return {false, "restricted"};
    return channel ? MemberStatus{false, "read_only"} : MemberStatus{};
  }
  case td_api::chatMemberStatusLeft::ID:
    return {false, channel ? "read_only" : "not_member"};
  case td_api::chatMemberStatusBanned::ID:
    return {false, "banned"};
  default:
    return {};
  }
}

std::string member_key(const std::string &kind, std::int64_t peer) {
  return (kind == "basic" ? "basic:" : "super:") + std::to_string(peer);
}

nlohmann::json can_send_projection(const Account &account, std::int64_t chat_id) {
  const auto blocked = [](const char *reason) { return nlohmann::json{{"text", false}, {"reason", reason}}; };
  const nlohmann::json allowed{{"text", true}, {"reason", nullptr}};
  const auto found = account.send_contexts.find(chat_id);
  if (found == account.send_contexts.end())
    return {{"text", nullptr}, {"reason", nullptr}};
  const auto &context = found->second;
  if (context.kind == "secret")
    return blocked("secret_chat");
  if (context.kind == "private")
    return account.deleted_users.contains(context.peer) ? blocked("user_deleted") : allowed;
  if (context.kind == "basic" || context.kind == "super" || context.kind == "channel") {
    const auto status = account.member_statuses.find(member_key(context.kind, context.peer));
    if (status != account.member_statuses.end()) {
      if (const auto &can_send = status->second.can_send; can_send.has_value())
        return can_send.value() ? allowed : blocked(status->second.reason.c_str());
    }
    if (context.kind == "channel")
      return status == account.member_statuses.end() ? nlohmann::json{{"text", nullptr}, {"reason", nullptr}}
                                                     : blocked("read_only");
    if (!context.basic_allowed)
      return blocked("restricted");
    return status == account.member_statuses.end() ? nlohmann::json{{"text", nullptr}, {"reason", nullptr}} : allowed;
  }
  return {{"text", nullptr}, {"reason", nullptr}};
}

void decorate_chat(const Account &account, nlohmann::json &chat) {
  chat["is_saved_messages"] = chat.value("type", "") == "private" && account.telegram_identity.is_object() &&
                              chat.value("id", "") == account.telegram_identity.value("id", "");
  if (const auto id = parse_int64(chat.value("id", std::string{"0"})))
    chat["can_send"] = chat.value("is_saved_messages", false) ? nlohmann::json{{"text", true}, {"reason", nullptr}}
                                                              : can_send_projection(account, *id);
  if (chat.value("last_message", nlohmann::json(nullptr)).is_object())
    decorate_sender(account, chat["last_message"]);
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
