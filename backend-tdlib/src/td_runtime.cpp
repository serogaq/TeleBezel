#include "telebezel/td_runtime.hpp"
#include "telebezel/crypto.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <td/telegram/td_api.h>

namespace telebezel {
namespace td_api = td::td_api;
namespace {
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
  if (command.contains("proxy") && command["proxy"].is_object()) {
    const auto &proxy = command["proxy"];
    command["proxy"] = proxy.contains("id") ? nlohmann::json{{"id", proxy["id"]}}
                                            : nlohmann::json{{"mode", proxy.value("mode", "inherit")}};
  }
  return sha256_hex(command.dump());
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
  return {{"id", std::to_string(message.id_)},
          {"chat_id", std::to_string(message.chat_id_)},
          {"sender", sender},
          {"date", message.date_},
          {"edit_date", message.edit_date_},
          {"is_outgoing", message.is_outgoing_},
          {"author_signature", message.author_signature_},
          {"content", message_content(message.content_.get())}};
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
      {"last_message", chat.last_message_ ? message_projection(*chat.last_message_) : nlohmann::json(nullptr)}};
  for (const auto &position : chat.positions_)
    apply_position(projection, position.get());
  return projection;
}
} // namespace

TdRuntime::TdRuntime(const Config &config, Registry &registry, std::unique_ptr<TdTransport> transport)
    : config_(config), registry_(registry),
      transport_(transport ? std::move(transport) : std::make_unique<NativeTdTransport>()) {}
TdRuntime::~TdRuntime() { stop(); }

void TdRuntime::start() {
  if (receive_thread_.joinable())
    return;
  td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(0));
  const auto version_object = td::ClientManager::execute(td_api::make_object<td_api::getOption>("version"));
  if (!version_object || version_object->get_id() != td_api::optionValueString::ID) {
    throw std::runtime_error("unable to read TDLib version");
  }
  const auto &version = static_cast<const td_api::optionValueString &>(*version_object);
  {
    std::lock_guard lock(mutex_);
    status_.ready = true;
    status_.tdlib_version = version.value_;
    for (const auto &manifest : registry_.discover()) {
      Account account;
      account.uuid = manifest.uuid;
      account.generation = manifest.generation;
      account.revision = manifest.revision;
      account.use_test_dc = manifest.use_test_dc;
      account.lifecycle = manifest.lifecycle;
      account.operation_id = manifest.operation_id;
      account.operation_phase = manifest.operation_phase;
      account.revision_fingerprint = manifest.content_hash;
      account.proxy = manifest.proxy;
      account.runtime_epoch = make_request_id();
      account.tombstone = manifest.tombstone;
      account.closed = manifest.tombstone;
      accounts_.emplace(account.uuid, std::move(account));
      std::cerr << nlohmann::json{{"event", "account_awaiting_reconciliation"},
                                  {"account_uuid", manifest.uuid},
                                  {"reason", "reconciliation_not_received"}}
                       .dump()
                << '\n';
    }
    for (const auto &uuid : registry_.orphan_account_ids()) {
      orphan_accounts_.insert(uuid);
      std::cerr << nlohmann::json{{"event", "account_storage_unregistered"},
                                  {"account_uuid", uuid},
                                  {"reason", "reconciliation_not_received"}}
                       .dump()
                << '\n';
    }
    manifest_errors_ = registry_.manifest_errors();
    for (const auto &[uuid, error] : manifest_errors_) {
      std::cerr
          << nlohmann::json{{"event", "account_storage_error"}, {"account_uuid", uuid}, {"error_code", error}}.dump()
          << '\n';
    }
    status_.account_count = accounts_.size();
  }
  stopping_.store(false);
  receive_done_.store(false);
  receive_thread_ = std::thread(&TdRuntime::receive_loop, this);
}

void TdRuntime::stop() {
  if (!receive_thread_.joinable())
    return;
  stopping_.store(true);
  std::vector<std::int32_t> clients;
  {
    std::lock_guard lock(mutex_);
    status_.ready = false;
    for (const auto &[uuid, account] : accounts_) {
      static_cast<void>(uuid);
      if (account.client_id != 0 && !account.closed)
        clients.push_back(account.client_id);
    }
    for (auto &[request_id, pending] : pending_) {
      static_cast<void>(request_id);
      pending.promise.set_value(td_api::make_object<td_api::error>(503, "service.stopping"));
    }
    pending_.clear();
  }
  for (const auto client : clients) {
    transport_->send(client, next_id(), td_api::make_object<td_api::close>());
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  {
    std::unique_lock lock(mutex_);
    condition_.wait_until(lock, deadline, [this] {
      return std::none_of(accounts_.begin(), accounts_.end(),
                          [](const auto &entry) { return entry.second.client_id != 0 && !entry.second.closed; });
    });
  }
  if (std::chrono::steady_clock::now() >= deadline) {
    std::cerr << nlohmann::json{{"event", "runtime_shutdown_timeout"}}.dump() << '\n';
  }
  receive_done_.store(true);
  receive_thread_.join();
}

StatusSnapshot TdRuntime::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

nlohmann::json TdRuntime::account_json(const Account &account) const {
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
      {"applied_revision", account.revision},
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

nlohmann::json TdRuntime::snapshots(const std::vector<std::string> &ids) const {
  std::lock_guard lock(mutex_);
  nlohmann::json result = nlohmann::json::object();
  if (ids.empty()) {
    for (const auto &[id, account] : accounts_) {
      result[id] = account_json(account);
    }
  } else {
    for (const auto &id : ids) {
      if (const auto iterator = accounts_.find(id); iterator != accounts_.end())
        result[id] = account_json(iterator->second);
    }
  }
  return {{"accounts", result}, {"unregistered_storage", orphan_accounts_}, {"manifest_errors", manifest_errors_}};
}

nlohmann::json TdRuntime::snapshot(const std::string &uuid) const {
  std::lock_guard lock(mutex_);
  const auto iterator = accounts_.find(uuid);
  return iterator == accounts_.end() ? safe_error("account.not_found", 404) : account_json(iterator->second);
}

void TdRuntime::append_event(Account &account, const std::string &type, std::int64_t chat_id, std::int64_t message_id) {
  nlohmann::json event{{"sequence", ++account.event_sequence}, {"type", type}, {"chat_id", std::to_string(chat_id)}};
  if (message_id != 0)
    event["message_id"] = std::to_string(message_id);
  account.events.push_back(std::move(event));
  while (account.events.size() > 5000)
    account.events.pop_front();
}

std::string TdRuntime::cursor_for(const Account &account, std::uint64_t sequence) const {
  const std::string payload =
      std::to_string(account.authorization_generation) + ":" + account.runtime_epoch + ":" + std::to_string(sequence);
  return payload + "." +
         sha256_hex(config_.internal_token + ":" + account.uuid + ":" + account.generation + ":" + payload);
}

std::optional<std::uint64_t> TdRuntime::parse_cursor(const Account &account, const std::string &cursor) const {
  const auto dot = cursor.rfind('.');
  if (dot == std::string::npos)
    return std::nullopt;
  const auto payload = cursor.substr(0, dot);
  const auto signature =
      sha256_hex(config_.internal_token + ":" + account.uuid + ":" + account.generation + ":" + payload);
  const auto prefix = std::to_string(account.authorization_generation) + ":" + account.runtime_epoch + ":";
  if (!token_matches(signature, cursor.substr(dot + 1)) || !payload.starts_with(prefix))
    return std::nullopt;
  try {
    std::size_t parsed = 0;
    const auto sequence = std::stoull(payload.substr(prefix.size()), &parsed);
    if (parsed != payload.size() - prefix.size())
      return std::nullopt;
    return sequence;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

nlohmann::json TdRuntime::chats(const std::string &uuid, const std::string &list, std::size_t limit,
                                const std::string &cursor) {
  std::int32_t client = 0;
  std::uint64_t lower_sequence = 0;
  std::uint64_t authorization_generation = 1;
  std::string generation;
  std::optional<std::pair<std::int64_t, std::int64_t>> boundary;
  {
    std::lock_guard lock(mutex_);
    const auto found = accounts_.find(uuid);
    if (found == accounts_.end() || !found->second.reconciled || found->second.authorization_state != "ready")
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
      request(client, td_api::make_object<td_api::getChats>(std::move(chat_list), 50), std::chrono::seconds(6));
  load_failed = !response || response->get_id() == td_api::error::ID;
  std::lock_guard lock(mutex_);
  const auto found = accounts_.find(uuid);
  if (found == accounts_.end() || found->second.tombstone)
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
          {"updates_cursor", cursor_for(account, lower_sequence)},
          {"observed_at", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
          {"source", load_failed ? "tdlib_memory" : "tdlib"}};
}

nlohmann::json TdRuntime::chat(const std::string &uuid, std::int64_t chat_id) const {
  std::lock_guard lock(mutex_);
  const auto account = accounts_.find(uuid);
  if (account == accounts_.end() || !account->second.reconciled)
    return safe_error("account.not_found", 404);
  const auto found = account->second.chats.find(chat_id);
  if (found == account->second.chats.end())
    return safe_error("chat.not_found", 404);
  return {{"item", found->second},
          {"stale", true},
          {"partial", false},
          {"updates_cursor", cursor_for(account->second, account->second.event_sequence)},
          {"source", "tdlib_memory"}};
}

nlohmann::json TdRuntime::messages(const std::string &uuid, std::int64_t chat_id, std::size_t limit,
                                   const std::string &cursor) {
  std::int32_t client = 0;
  std::uint64_t lower_sequence = 0;
  std::uint64_t authorization_generation = 1;
  std::string epoch;
  std::string generation;
  std::int64_t anchor = 0;
  {
    std::lock_guard lock(mutex_);
    const auto account = accounts_.find(uuid);
    if (account == accounts_.end() || !account->second.reconciled || account->second.client_id == 0)
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
  auto response = request(
      client, td_api::make_object<td_api::getChatHistory>(chat_id, anchor, 0, static_cast<std::int32_t>(limit), false),
      std::chrono::seconds(6));
  if (!response || response->get_id() == td_api::error::ID) {
    fallback = true;
    response = request(
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
  std::lock_guard lock(mutex_);
  const auto account = accounts_.find(uuid);
  if (account == accounts_.end() || account->second.runtime_epoch != epoch)
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
      if (iterator->first.first == chat_id && (anchor == 0 || iterator->first.second <= anchor))
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
          {"updates_cursor", cursor_for(account->second, lower_sequence)},
          {"observed_at", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
          {"source", fallback ? "tdlib_local" : "tdlib"}};
}

nlohmann::json TdRuntime::message(const std::string &uuid, std::int64_t chat_id, std::int64_t message_id) {
  std::int32_t client = 0;
  std::uint64_t lower_sequence = 0;
  std::string epoch;
  {
    std::lock_guard lock(mutex_);
    const auto account = accounts_.find(uuid);
    if (account == accounts_.end() || !account->second.reconciled)
      return safe_error("account.not_found", 404);
    const auto found = account->second.messages.find({chat_id, message_id});
    if (found != account->second.messages.end())
      return {{"item", found->second},
              {"stale", true},
              {"partial", false},
              {"updates_cursor", cursor_for(account->second, account->second.event_sequence)},
              {"source", "tdlib_memory"}};
    client = account->second.client_id;
    lower_sequence = account->second.event_sequence;
    epoch = account->second.runtime_epoch;
  }
  const auto response =
      request(client, td_api::make_object<td_api::getMessage>(chat_id, message_id), std::chrono::seconds(6));
  if (!response || response->get_id() != td_api::message::ID)
    return safe_error("message.not_found", 404);
  const auto projection = message_projection(static_cast<const td_api::message &>(*response));
  std::lock_guard lock(mutex_);
  auto &account = accounts_.at(uuid);
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
              {"updates_cursor", cursor_for(account, lower_sequence)},
              {"source", "tdlib_memory"}};
    return safe_error("sync.resync_required", 409);
  }
  account.messages[{chat_id, message_id}] = projection;
  return {{"item", projection},
          {"stale", true},
          {"partial", false},
          {"updates_cursor", cursor_for(account, account.event_sequence)},
          {"source", "tdlib"}};
}

nlohmann::json TdRuntime::updates(const std::string &uuid, const std::string &cursor, std::size_t limit) const {
  std::lock_guard lock(mutex_);
  const auto found = accounts_.find(uuid);
  if (found == accounts_.end() || !found->second.reconciled)
    return safe_error("account.not_found", 404);
  const auto &account = found->second;
  std::uint64_t sequence = account.event_sequence;
  if (!cursor.empty()) {
    const auto parsed = parse_cursor(account, cursor);
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
  return {{"items", items}, {"cursor", cursor_for(account, last)}, {"has_more", last < account.event_sequence}};
}

nlohmann::json TdRuntime::set_interest(const std::string &uuid, std::int64_t chat_id, const std::string &lease_key,
                                       bool active) {
  std::int32_t client = 0;
  bool transition = false;
  {
    std::lock_guard lock(mutex_);
    const auto found = accounts_.find(uuid);
    if (found == accounts_.end() || !found->second.reconciled || found->second.client_id == 0)
      return safe_error("account.not_found", 404);
    auto &account = found->second;
    const auto now = std::chrono::steady_clock::now();
    for (auto iterator = account.interests.begin(); iterator != account.interests.end();) {
      if (iterator->second.second > now) {
        ++iterator;
        continue;
      }
      auto &count = account.interest_counts[iterator->second.first];
      if (count > 0)
        --count;
      iterator = account.interests.erase(iterator);
    }
    const auto existing = account.interests.find(lease_key);
    if (active) {
      if (existing == account.interests.end()) {
        if (account.interests.size() >= 20)
          return safe_error("interest.limit_reached", 429);
        const auto first_separator = lease_key.find(':');
        const auto second_separator =
            first_separator == std::string::npos ? std::string::npos : lease_key.find(':', first_separator + 1);
        const auto principal_prefix =
            second_separator == std::string::npos ? lease_key : lease_key.substr(0, second_separator + 1);
        const auto principal_count =
            std::count_if(account.interests.begin(), account.interests.end(),
                          [&principal_prefix](const auto &entry) { return entry.first.starts_with(principal_prefix); });
        if (principal_count >= 4)
          return safe_error("interest.limit_reached", 429);
        transition = account.interest_counts[chat_id]++ == 0;
      }
      account.interests[lease_key] = {chat_id, now + std::chrono::seconds(90)};
    } else if (existing != account.interests.end()) {
      auto &count = account.interest_counts[existing->second.first];
      transition = count > 0 && --count == 0;
      account.interests.erase(existing);
    }
    client = account.client_id;
  }
  if (transition) {
    const auto response = active ? request(client, td_api::make_object<td_api::openChat>(chat_id))
                                 : request(client, td_api::make_object<td_api::closeChat>(chat_id));
    if (!response || response->get_id() == td_api::error::ID) {
      if (active) {
        std::lock_guard lock(mutex_);
        if (auto account = accounts_.find(uuid); account != accounts_.end()) {
          if (auto lease = account->second.interests.find(lease_key); lease != account->second.interests.end()) {
            auto &count = account->second.interest_counts[lease->second.first];
            if (count > 0)
              --count;
            account->second.interests.erase(lease);
          }
        }
      }
      return safe_error("telegram.operation_failed", 502);
    }
    bool compensate = false;
    {
      std::lock_guard lock(mutex_);
      if (const auto account = accounts_.find(uuid); account != accounts_.end()) {
        const auto count = account->second.interest_counts.find(chat_id);
        const bool wanted_open = count != account->second.interest_counts.end() && count->second > 0;
        compensate = wanted_open != active;
      }
    }
    if (compensate) {
      if (active)
        transport_->send(client, next_id(), td_api::make_object<td_api::closeChat>(chat_id));
      else
        transport_->send(client, next_id(), td_api::make_object<td_api::openChat>(chat_id));
    }
  }
  return {{"active", active}, {"expires_in", active ? 90 : 0}};
}

nlohmann::json TdRuntime::release_interests(const std::string &principal_type, const std::string &principal_id) {
  const auto prefix = principal_type + ":" + principal_id + ":";
  std::vector<std::pair<std::int32_t, std::int64_t>> closes;
  std::size_t released = 0;
  {
    std::lock_guard lock(mutex_);
    for (auto &[uuid, account] : accounts_) {
      static_cast<void>(uuid);
      for (auto iterator = account.interests.begin(); iterator != account.interests.end();) {
        if (!iterator->first.starts_with(prefix)) {
          ++iterator;
          continue;
        }
        const auto chat_id = iterator->second.first;
        auto &count = account.interest_counts[chat_id];
        if (count > 0 && --count == 0 && account.client_id != 0)
          closes.emplace_back(account.client_id, chat_id);
        iterator = account.interests.erase(iterator);
        ++released;
      }
    }
  }
  for (const auto &[client, chat_id] : closes)
    transport_->send(client, next_id(), td_api::make_object<td_api::closeChat>(chat_id));
  return {{"released", released}};
}

TdRuntime::Account &TdRuntime::validated_account(const std::string &uuid, const nlohmann::json &command) {
  if (!valid_uuid(uuid) || command.value("uuid", "") != uuid || !valid_uuid(command.value("generation", "")) ||
      !command.contains("revision") || !command["revision"].is_number_integer() ||
      command["revision"].get<std::int64_t>() < 1) {
    throw std::runtime_error("request.invalid");
  }
  if (manifest_errors_.contains(uuid))
    throw std::runtime_error(manifest_errors_.at(uuid));
  if (orphan_accounts_.contains(uuid) && command.value("lifecycle", "") != "provisioning")
    throw std::runtime_error("storage.corrupt");
  auto iterator = accounts_.find(uuid);
  if (iterator == accounts_.end()) {
    Account account;
    account.uuid = uuid;
    account.generation = command.at("generation").get<std::string>();
    account.use_test_dc = config_.use_test_dc;
    account.runtime_epoch = make_request_id();
    iterator = accounts_.emplace(uuid, std::move(account)).first;
    status_.account_count = accounts_.size();
  }
  auto &account = iterator->second;
  if (account.use_test_dc != config_.use_test_dc)
    throw std::runtime_error("configuration.environment_mismatch");
  if (account.generation != command.at("generation").get<std::string>())
    throw std::runtime_error("storage.identity_mismatch");
  const auto revision = command.at("revision").get<std::uint64_t>();
  if (revision < account.revision)
    throw std::runtime_error("operation.conflict");
  const auto authorization_generation = command.value("authorization_generation", account.authorization_generation);
  if (authorization_generation < account.authorization_generation)
    throw std::runtime_error("operation.conflict");
  account.authorization_generation = authorization_generation;
  if (account.tombstone)
    throw std::runtime_error("account.gone");
  return account;
}

nlohmann::json TdRuntime::reconcile(const std::string &uuid, const nlohmann::json &command) {
  Account *account = nullptr;
  const std::string fingerprint = command_fingerprint(command);
  {
    std::lock_guard lock(mutex_);
    account = &validated_account(uuid, command);
    const auto revision = command.at("revision").get<std::uint64_t>();
    if (revision == account->revision && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      if (account->reconciled) {
        auto result = account_json(*account);
        result["completed"] = account->operation_id.empty();
        return result;
      }
    }
    if (command.value("mode", "") == "restore" && !registry_.account_directory_exists(uuid))
      return safe_error("storage.missing", 409);
    if (account->busy)
      return safe_error("operation.conflict", 409);
    const auto requested_proxy = command.value("proxy", nlohmann::json::object());
    if (requested_proxy.size() == 1 && requested_proxy.contains("id") &&
        account->proxy.value("id", "") != requested_proxy.value("id", ""))
      return safe_error("configuration.missing", 409);
    account->busy = true;
    account->revision = revision;
    account->authorization_generation = command.value("authorization_generation", 1ULL);
    account->effective_config_id = command.value("effective_config_id", "");
    account->telegram_api_id = command.value("telegram_api_id", config_.telegram_api_id);
    account->telegram_api_hash = command.value("telegram_api_hash", config_.telegram_api_hash);
    account->lifecycle = command.value("lifecycle", "provisioning");
    if (!(requested_proxy.size() == 1 && requested_proxy.contains("id"))) {
      account->proxy = requested_proxy;
    }
    account->operation_id = nullable_string(command, "operation_id");
    account->revision_fingerprint = fingerprint;
  }
  try {
    registry_.ensure_account_directories(uuid);
    AccountManifest manifest{1,
                             uuid,
                             account->generation,
                             config_.use_test_dc,
                             1,
                             account->revision,
                             account->lifecycle,
                             account->operation_id,
                             false,
                             nullptr,
                             "",
                             ""};
    manifest.content_hash = account->revision_fingerprint;
    registry_.write(manifest, account->proxy);
    {
      std::lock_guard lock(mutex_);
      orphan_accounts_.erase(uuid);
    }
    activate(*account);
    nlohmann::json proxy;
    AccountManifest completed;
    {
      std::lock_guard lock(mutex_);
      account->reconciled = true;
      account->busy = false;
      account->lifecycle = "active";
      account->last_error.clear();
      completed = manifest;
      completed.lifecycle = "active";
      proxy = account->proxy;
    }
    registry_.write(completed, proxy);
    std::lock_guard lock(mutex_);
    const auto waited =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - account->discovered_at)
            .count();
    std::cerr << nlohmann::json{{"event", "account_reconciled"}, {"account_uuid", uuid}, {"awaited_ms", waited}}.dump()
              << '\n';
    return account_json(*account);
  } catch (const std::exception &exception) {
    std::lock_guard lock(mutex_);
    const std::string detail = exception.what();
    account->busy = false;
    if (detail == "operation.outcome_unknown") {
      account->last_error = detail;
      return safe_error(detail, 504);
    }
    account->last_error = detail.starts_with("storage.") || detail.starts_with("configuration.") ||
                                  detail.starts_with("service.") || detail.starts_with("telegram.")
                              ? detail
                              : "configuration.invalid";
    return safe_error(account->last_error, account->last_error.starts_with("service.") ? 503 : 409);
  }
}

void TdRuntime::activate(Account &account) {
  std::string uuid;
  const auto telegram_api_id = account.telegram_api_id == 0 ? config_.telegram_api_id : account.telegram_api_id;
  const auto &telegram_api_hash =
      account.telegram_api_hash.empty() ? config_.telegram_api_hash : account.telegram_api_hash;
  {
    std::lock_guard lock(mutex_);
    if (account.client_id != 0 && account.closing)
      throw std::runtime_error("operation.outcome_unknown");
    if (account.client_id != 0 && !account.closed)
      return;
    uuid = account.uuid;
  }
  if (telegram_api_id == 0 || telegram_api_hash.empty() || config_.master_key_file.empty()) {
    throw std::runtime_error("configuration.missing");
  }
  if (!std::filesystem::exists(config_.master_key_file))
    throw std::runtime_error("configuration.missing");
  std::array<std::uint8_t, 32> master_key{};
  try {
    master_key = read_master_key(config_.master_key_file);
  } catch (const std::exception &) {
    throw std::runtime_error("storage.invalid_key");
  }
  const auto derived = derive_database_key(master_key, uuid);
  const auto client = transport_->create_client_id();
  {
    std::lock_guard lock(mutex_);
    account.client_id = client;
    account.closed = false;
    client_accounts_[client] = uuid;
  }
  try {
    const auto root = registry_.root() / "accounts" / uuid;
    auto network_barrier = begin_request(
        client, td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeNone>()));
    const std::string key(reinterpret_cast<const char *>(derived.data()), derived.size());
    auto parameters = begin_request(client, td_api::make_object<td_api::setTdlibParameters>(
                                                config_.use_test_dc, (root / "db").string(), (root / "files").string(),
                                                key, true, true, false, false, telegram_api_id, telegram_api_hash, "en",
                                                "TeleBezel", "Linux", TELEBEZEL_SERVICE_VERSION));
    auto response = await_request(std::move(network_barrier), std::chrono::seconds(8));
    auto parameters_response = await_request(std::move(parameters), std::chrono::seconds(8));
    throw_runtime_control_error(response);
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
    response = std::move(parameters_response);
    throw_runtime_control_error(response);
    if (td_error_code(response, 401))
      throw std::runtime_error("storage.invalid_key");
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
    apply_proxy(account);
    response =
        request(client, td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeOther>()));
    throw_runtime_control_error(response);
    if (!response || response->get_id() == td_api::error::ID)
      throw std::runtime_error("configuration.invalid");
  } catch (...) {
    const auto failure = std::current_exception();
    {
      std::lock_guard lock(mutex_);
      if (account.client_id == client)
        account.closing = true;
    }
    transport_->send(client, next_id(), td_api::make_object<td_api::close>());
    std::unique_lock lock(mutex_);
    condition_.wait_for(lock, std::chrono::seconds(8),
                        [&account, client] { return account.client_id != client || account.closed; });
    lock.unlock();
    std::rethrow_exception(failure);
  }
}

void TdRuntime::apply_proxy(Account &account) {
  std::int32_t client = 0;
  nlohmann::json desired;
  {
    std::lock_guard lock(mutex_);
    client = account.client_id;
    desired = account.proxy;
  }
  const auto disabled = request(client, td_api::make_object<td_api::disableProxy>());
  throw_runtime_control_error(disabled);
  if (!disabled || disabled->get_id() == td_api::error::ID)
    throw std::runtime_error("configuration.invalid");
  auto existing = request(client, td_api::make_object<td_api::getProxies>());
  if (existing && existing->get_id() == td_api::addedProxies::ID) {
    auto proxies = td::move_tl_object_as<td_api::addedProxies>(existing);
    for (const auto &proxy : proxies->proxies_) {
      if (proxy && proxy->comment_ == "telebezel-managed") {
        const auto removed = request(client, td_api::make_object<td_api::removeProxy>(proxy->id_));
        throw_runtime_control_error(removed);
        if (!removed || removed->get_id() == td_api::error::ID)
          throw std::runtime_error("configuration.invalid");
      }
    }
  }
  const std::string mode = desired.value("mode", "inherit");
  if (mode != "inherit" && mode != "direct" && !valid_uuid(desired.value("id", "")))
    throw std::runtime_error("configuration.invalid");
  const ProxyConfig *selected = &config_.proxy;
  ProxyConfig override;
  if (mode != "inherit") {
    if (mode == "direct")
      selected = nullptr;
    else {
      override.mode = parse_proxy_mode(mode);
      override.host = desired.value("host", "");
      override.port = desired.value("port", 0);
      override.http_only = desired.value("http_only", false);
      override.username = desired.value("username", "");
      override.password = desired.value("password", "");
      override.secret = desired.value("secret", "");
      selected = &override;
    }
  }
  if (selected == nullptr || selected->mode == ProxyMode::direct || selected->mode == ProxyMode::inherit) {
    return;
  }
  if (selected->host.empty() || selected->port == 0 ||
      (selected->mode == ProxyMode::mtproto &&
       (selected->secret.empty() || !selected->username.empty() || !selected->password.empty())) ||
      ((selected->mode == ProxyMode::socks5 || selected->mode == ProxyMode::http) && !selected->secret.empty()) ||
      (!selected->password.empty() && selected->username.empty()) ||
      (selected->mode != ProxyMode::http && selected->http_only)) {
    throw std::runtime_error("configuration.invalid");
  }
  td_api::object_ptr<td_api::ProxyType> type;
  if (selected->mode == ProxyMode::socks5)
    type = td_api::make_object<td_api::proxyTypeSocks5>(selected->username, selected->password);
  else if (selected->mode == ProxyMode::http)
    type = td_api::make_object<td_api::proxyTypeHttp>(selected->username, selected->password, selected->http_only);
  else
    type = td_api::make_object<td_api::proxyTypeMtproto>(selected->secret);
  auto proxy = td_api::make_object<td_api::proxy>(selected->host, selected->port, std::move(type));
  const auto response =
      request(client, td_api::make_object<td_api::addProxy>(std::move(proxy), true, "telebezel-managed"));
  throw_runtime_control_error(response);
  if (!response || response->get_id() != td_api::addedProxy::ID)
    throw std::runtime_error("configuration.invalid");
}

std::uint64_t TdRuntime::next_id() {
  auto value = next_request_id_.fetch_add(1);
  if (value == 0)
    value = next_request_id_.fetch_add(1);
  return value;
}

td_api::object_ptr<td_api::Object> TdRuntime::request(std::int32_t client_id,
                                                      td_api::object_ptr<td_api::Function> function,
                                                      std::chrono::seconds timeout, bool recover_stalled_client) {
  return await_request(begin_request(client_id, std::move(function)), timeout, recover_stalled_client);
}

TdRuntime::RequestHandle TdRuntime::begin_request(std::int32_t client_id,
                                                  td_api::object_ptr<td_api::Function> function) {
  std::promise<td_api::object_ptr<td_api::Object>> immediate;
  auto immediate_future = immediate.get_future();
  const bool stopping = stopping_.load();
  if (stopping)
    immediate.set_value(td_api::make_object<td_api::error>(503, "service.stopping"));
  if (stopping)
    return {0, client_id, std::move(immediate_future), false};
  const auto request_id = next_id();
  std::future<td_api::object_ptr<td_api::Object>> future;
  {
    std::lock_guard lock(mutex_);
    if (pending_.size() + async_requests_.size() >= 1024) {
      immediate.set_value(td_api::make_object<td_api::error>(503, "service.busy"));
      return {0, client_id, std::move(immediate_future), false};
    }
    Pending pending{client_id, {}};
    future = pending.promise.get_future();
    pending_.emplace(request_id, std::move(pending));
  }
  transport_->send(client_id, request_id, std::move(function));
  return {request_id, client_id, std::move(future), true};
}

td_api::object_ptr<td_api::Object> TdRuntime::await_request(RequestHandle handle, std::chrono::seconds timeout,
                                                            bool recover_stalled_client) {
  if (handle.future.wait_for(timeout) != std::future_status::ready) {
    std::lock_guard lock(mutex_);
    pending_.erase(handle.request_id);
    if (unresolved_.size() >= 1024)
      unresolved_.erase(unresolved_.begin());
    if (handle.pending) {
      unresolved_[handle.request_id] = {handle.client_id, std::chrono::steady_clock::now() + std::chrono::seconds(22),
                                        recover_stalled_client};
    }
    return td_api::make_object<td_api::error>(504, "operation.outcome_unknown");
  }
  return handle.future.get();
}

void TdRuntime::request_identity(Account &account) {
  if (account.client_id == 0 || async_requests_.size() + pending_.size() >= 1024) {
    if (account.client_id != 0)
      account.last_error = "service.busy";
    return;
  }
  const auto request_id = next_id();
  async_requests_.emplace(request_id, AsyncRequest{account.client_id, AsyncRequestKind::get_me,
                                                   std::chrono::steady_clock::now() + std::chrono::seconds(8)});
  transport_->send(account.client_id, request_id, td_api::make_object<td_api::getMe>());
}

void TdRuntime::receive_loop() {
  auto next_reminder = std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (!receive_done_.load()) {
    std::vector<std::int32_t> stalled_clients;
    std::vector<std::pair<std::int32_t, std::int64_t>> expired_chats;
    {
      std::lock_guard lock(mutex_);
      const auto now = std::chrono::steady_clock::now();
      for (auto &[request_id, unresolved] : unresolved_) {
        static_cast<void>(request_id);
        if (unresolved.recover_client && unresolved.recovery_at <= now) {
          stalled_clients.push_back(unresolved.client_id);
          unresolved.recover_client = false;
        }
      }
      for (auto iterator = async_requests_.begin(); iterator != async_requests_.end();) {
        if (iterator->second.deadline <= now)
          iterator = async_requests_.erase(iterator);
        else
          ++iterator;
      }
      for (auto &[uuid, account] : accounts_) {
        static_cast<void>(uuid);
        for (auto iterator = account.interests.begin(); iterator != account.interests.end();) {
          if (iterator->second.second > now) {
            ++iterator;
            continue;
          }
          const auto chat_id = iterator->second.first;
          auto &count = account.interest_counts[chat_id];
          if (count > 0 && --count == 0 && account.client_id != 0)
            expired_chats.emplace_back(account.client_id, chat_id);
          iterator = account.interests.erase(iterator);
        }
      }
    }
    for (const auto client : stalled_clients) {
      transport_->send(client, next_id(), td_api::make_object<td_api::close>());
    }
    for (const auto &[client, chat_id] : expired_chats)
      transport_->send(client, next_id(), td_api::make_object<td_api::closeChat>(chat_id));
    auto response = transport_->receive(0.1);
    if (!response.object) {
      if (std::chrono::steady_clock::now() >= next_reminder) {
        std::lock_guard lock(mutex_);
        for (const auto &[uuid, account] : accounts_) {
          if (!account.reconciled && !account.closed) {
            std::cerr << nlohmann::json{{"event", "account_awaiting_reconciliation"},
                                        {"account_uuid", uuid},
                                        {"reason", "reconciliation_not_received"}}
                             .dump()
                      << '\n';
          }
        }
        next_reminder = std::chrono::steady_clock::now() + std::chrono::minutes(5);
      }
      continue;
    }
    std::lock_guard lock(mutex_);
    if (response.request_id == 0) {
      handle_update(response.client_id, *response.object);
      continue;
    }
    const auto asynchronous = async_requests_.find(response.request_id);
    if (asynchronous != async_requests_.end()) {
      if (asynchronous->second.client_id == response.client_id &&
          asynchronous->second.kind == AsyncRequestKind::get_me && response.object->get_id() == td_api::user::ID) {
        const auto mapped = client_accounts_.find(response.client_id);
        if (mapped != client_accounts_.end()) {
          const auto &user = static_cast<const td_api::user &>(*response.object);
          std::vector<std::string> usernames;
          if (user.usernames_)
            usernames = user.usernames_->active_usernames_;
          accounts_.at(mapped->second).telegram_identity = {{"id", std::to_string(user.id_)},
                                                            {"first_name", user.first_name_},
                                                            {"last_name", user.last_name_},
                                                            {"usernames", usernames},
                                                            {"is_premium", user.is_premium_}};
        }
      }
      async_requests_.erase(asynchronous);
      continue;
    }
    const auto pending = pending_.find(response.request_id);
    if (pending == pending_.end()) {
      const auto unresolved = unresolved_.find(response.request_id);
      if (unresolved != unresolved_.end() && unresolved->second.client_id == response.client_id) {
        unresolved_.erase(unresolved);
        std::cerr << nlohmann::json{{"event", "tdlib_late_response"}, {"request_id", response.request_id}}.dump()
                  << '\n';
      }
      continue;
    }
    if (pending->second.client_id != response.client_id)
      continue;
    pending->second.promise.set_value(std::move(response.object));
    pending_.erase(pending);
  }
}

void TdRuntime::handle_update(std::int32_t client_id, td_api::Object &object) {
  const auto mapped = client_accounts_.find(client_id);
  if (mapped == client_accounts_.end())
    return;
  auto &account = accounts_.at(mapped->second);
  if (object.get_id() == td_api::updateAuthorizationState::ID) {
    auto &update = static_cast<td_api::updateAuthorizationState &>(object);
    const auto state_id = update.authorization_state_ ? update.authorization_state_->get_id() : 0;
    account.authorization_state = authorization_name(state_id);
    account.authorization_version = std::to_string(next_id());
    account.qr_link.clear();
    account.delivery_method.clear();
    account.delivery_supported = true;
    account.resend_available_at = {};
    if (account.authorization_state == "error") {
      account.last_error = "authorization.unsupported_state";
    } else if (account.last_error == "authorization.unsupported_state" ||
               account.last_error == "authorization.unsupported_delivery") {
      account.last_error.clear();
    }
    if (state_id == td_api::authorizationStateWaitCode::ID) {
      const auto &state = static_cast<td_api::authorizationStateWaitCode &>(*update.authorization_state_);
      if (state.code_info_) {
        const auto [method, supported] = delivery_method(state.code_info_->type_.get());
        account.delivery_method = method;
        account.delivery_supported = supported;
        account.resend_available_at =
            std::chrono::steady_clock::now() + std::chrono::seconds(std::max(0, state.code_info_->timeout_));
        if (!supported)
          account.last_error = "authorization.unsupported_delivery";
      }
    } else if (state_id == td_api::authorizationStateWaitEmailCode::ID) {
      account.delivery_method = "email";
    }
    if (state_id == td_api::authorizationStateWaitOtherDeviceConfirmation::ID) {
      account.qr_link =
          static_cast<td_api::authorizationStateWaitOtherDeviceConfirmation &>(*update.authorization_state_).link_;
    }
    if (state_id == td_api::authorizationStateClosed::ID) {
      account.closed = true;
      account.closing = false;
      if (account.client_id == client_id)
        account.client_id = 0;
      client_accounts_.erase(mapped);
      if (account.lifecycle != "logout_pending" && account.lifecycle != "removing")
        account.reconciled = false;
      condition_.notify_all();
    } else if (state_id == td_api::authorizationStateReady::ID) {
      request_identity(account);
    }
  } else if (object.get_id() == td_api::updateConnectionState::ID) {
    auto &update = static_cast<td_api::updateConnectionState &>(object);
    account.connection_state = update.state_ ? connection_name(update.state_->get_id()) : "unknown";
  } else if (object.get_id() == td_api::updateNewChat::ID) {
    auto &update = static_cast<td_api::updateNewChat &>(object);
    if (update.chat_) {
      account.chats[update.chat_->id_] = chat_projection(*update.chat_);
      if (update.chat_->last_message_)
        account.messages[{update.chat_->id_, update.chat_->last_message_->id_}] =
            message_projection(*update.chat_->last_message_);
      append_event(account, "chat_changed", update.chat_->id_);
    }
  } else if (object.get_id() == td_api::updateChatTitle::ID) {
    auto &update = static_cast<td_api::updateChatTitle &>(object);
    account.chats[update.chat_id_]["id"] = std::to_string(update.chat_id_);
    account.chats[update.chat_id_]["title"] = update.title_;
    append_event(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatPosition::ID) {
    auto &update = static_cast<td_api::updateChatPosition &>(object);
    account.chats[update.chat_id_]["id"] = std::to_string(update.chat_id_);
    if (!account.chats[update.chat_id_].contains("positions"))
      account.chats[update.chat_id_]["positions"] = nlohmann::json::object();
    apply_position(account.chats[update.chat_id_], update.position_.get());
    append_event(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatLastMessage::ID) {
    auto &update = static_cast<td_api::updateChatLastMessage &>(object);
    auto &chat = account.chats[update.chat_id_];
    chat["id"] = std::to_string(update.chat_id_);
    chat["positions"] = nlohmann::json::object();
    for (const auto &position : update.positions_)
      apply_position(chat, position.get());
    chat["last_message"] = update.last_message_ ? message_projection(*update.last_message_) : nlohmann::json(nullptr);
    if (update.last_message_)
      account.messages[{update.chat_id_, update.last_message_->id_}] = message_projection(*update.last_message_);
    append_event(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatDraftMessage::ID) {
    auto &update = static_cast<td_api::updateChatDraftMessage &>(object);
    auto &chat = account.chats[update.chat_id_];
    chat["id"] = std::to_string(update.chat_id_);
    chat["positions"] = nlohmann::json::object();
    for (const auto &position : update.positions_)
      apply_position(chat, position.get());
    append_event(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateNewMessage::ID) {
    auto &update = static_cast<td_api::updateNewMessage &>(object);
    if (update.message_) {
      account.messages[{update.message_->chat_id_, update.message_->id_}] = message_projection(*update.message_);
      append_event(account, "message_changed", update.message_->chat_id_, update.message_->id_);
    }
  } else if (object.get_id() == td_api::updateMessageContent::ID) {
    auto &update = static_cast<td_api::updateMessageContent &>(object);
    auto found = account.messages.find({update.chat_id_, update.message_id_});
    if (found != account.messages.end())
      found->second["content"] = message_content(update.new_content_.get());
    auto &last = account.chats[update.chat_id_]["last_message"];
    if (last.is_object() && last.value("id", "") == std::to_string(update.message_id_)) {
      last["content"] = message_content(update.new_content_.get());
      append_event(account, "chat_changed", update.chat_id_);
    }
    append_event(account, "message_changed", update.chat_id_, update.message_id_);
  } else if (object.get_id() == td_api::updateMessageEdited::ID) {
    auto &update = static_cast<td_api::updateMessageEdited &>(object);
    auto found = account.messages.find({update.chat_id_, update.message_id_});
    if (found != account.messages.end())
      found->second["edit_date"] = update.edit_date_;
    auto &last = account.chats[update.chat_id_]["last_message"];
    if (last.is_object() && last.value("id", "") == std::to_string(update.message_id_)) {
      last["edit_date"] = update.edit_date_;
      append_event(account, "chat_changed", update.chat_id_);
    }
    append_event(account, "message_changed", update.chat_id_, update.message_id_);
  } else if (object.get_id() == td_api::updateDeleteMessages::ID) {
    auto &update = static_cast<td_api::updateDeleteMessages &>(object);
    for (const auto message_id : update.message_ids_) {
      account.messages.erase({update.chat_id_, message_id});
      append_event(account, update.from_cache_ ? "message_changed" : "message_deleted", update.chat_id_, message_id);
      auto &last = account.chats[update.chat_id_]["last_message"];
      if (last.is_object() && last.value("id", "") == std::to_string(message_id)) {
        last = nullptr;
        append_event(account, "chat_changed", update.chat_id_);
      }
    }
  } else if (object.get_id() == td_api::updateChatReadInbox::ID) {
    auto &update = static_cast<td_api::updateChatReadInbox &>(object);
    auto &chat = account.chats[update.chat_id_];
    chat["last_read_inbox_message_id"] = std::to_string(update.last_read_inbox_message_id_);
    chat["unread_count"] = update.unread_count_;
    append_event(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatReadOutbox::ID) {
    auto &update = static_cast<td_api::updateChatReadOutbox &>(object);
    account.chats[update.chat_id_]["last_read_outbox_message_id"] = std::to_string(update.last_read_outbox_message_id_);
    append_event(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatIsMarkedAsUnread::ID) {
    auto &update = static_cast<td_api::updateChatIsMarkedAsUnread &>(object);
    account.chats[update.chat_id_]["is_marked_unread"] = update.is_marked_as_unread_;
    append_event(account, "chat_changed", update.chat_id_);
  }
}

nlohmann::json TdRuntime::authorization_action(const std::string &uuid, const nlohmann::json &command) {
  std::int32_t client = 0;
  std::string action;
  std::string state;
  {
    std::lock_guard lock(mutex_);
    auto &account = validated_account(uuid, command);
    action = command.value("action", "");
    const bool needs_value = action != "start_qr" && action != "resend_code";
    if ((needs_value && (!command.contains("value") || !command["value"].is_string() ||
                         command["value"].get_ref<const std::string &>().empty())) ||
        (!needs_value && command.contains("value"))) {
      throw std::runtime_error("request.invalid");
    }
    const auto actions = allowed_actions(account);
    if (!account.reconciled || command.value("authorization_version", "") != account.authorization_version ||
        std::find(actions.begin(), actions.end(), action) == actions.end()) {
      return safe_error("authorization.invalid_state", 409);
    }
    if (account.busy)
      return safe_error("operation.conflict", 409);
    account.busy = true;
    client = account.client_id;
    state = account.authorization_state;
  }
  td_api::object_ptr<td_api::Function> function;
  const std::string value = command.value("value", "");
  if (action == "submit_phone_number") {
    function = td_api::make_object<td_api::setAuthenticationPhoneNumber>(
        value, td_api::make_object<td_api::phoneNumberAuthenticationSettings>());
  } else if (action == "submit_code")
    function = td_api::make_object<td_api::checkAuthenticationCode>(value);
  else if (action == "submit_password")
    function = td_api::make_object<td_api::checkAuthenticationPassword>(value);
  else if (action == "submit_email_address")
    function = td_api::make_object<td_api::setAuthenticationEmailAddress>(value);
  else if (action == "submit_email_code") {
    function = td_api::make_object<td_api::checkAuthenticationEmailCode>(
        td_api::make_object<td_api::emailAddressAuthenticationCode>(value));
  } else if (action == "start_qr") {
    function = td_api::make_object<td_api::requestQrCodeAuthentication>(std::vector<std::int64_t>{});
  } else if (state == "awaiting_email_code") {
    function = td_api::make_object<td_api::resendLoginEmailAddressCode>();
  } else {
    function = td_api::make_object<td_api::resendAuthenticationCode>(
        td_api::make_object<td_api::resendCodeReasonUserRequest>());
  }
  auto response = request(client, std::move(function), std::chrono::seconds(8), true);
  std::lock_guard lock(mutex_);
  auto &account = accounts_.at(uuid);
  account.busy = false;
  if (!response || response->get_id() == td_api::error::ID) {
    const int code =
        response && response->get_id() == td_api::error::ID ? static_cast<td_api::error &>(*response).code_ : 500;
    if (code == 504)
      return safe_error("operation.outcome_unknown", 504);
    const std::string message = response && response->get_id() == td_api::error::ID
                                    ? static_cast<td_api::error &>(*response).message_
                                    : std::string{};
    const std::string error =
        code == 429 || message.find("FLOOD_WAIT") != std::string::npos ? "authorization.flood_wait"
        : message.find("CODE_EXPIRED") != std::string::npos            ? "authorization.code_expired"
        : message.find("CODE_INVALID") != std::string::npos            ? "authorization.invalid_code"
        : message.find("PASSWORD_HASH_INVALID") != std::string::npos   ? "authorization.invalid_password"
        : action == "submit_code" || action == "submit_email_code"     ? "authorization.invalid_code"
        : action == "submit_password"                                  ? "authorization.invalid_password"
                                                                       : "telegram.operation_failed";
    return safe_error(error, code == 429 ? 429 : 422);
  }
  return account_json(account);
}

nlohmann::json TdRuntime::logout(const std::string &uuid, const nlohmann::json &command) {
  Account *account = nullptr;
  bool new_operation = false;
  const std::string fingerprint = command_fingerprint(command);
  const std::string operation_id = nullable_string(command, "logout_operation_id");
  {
    std::lock_guard lock(mutex_);
    account = &validated_account(uuid, command);
    const auto revision = command.at("revision").get<std::uint64_t>();
    if (account->revision == revision && !account->revision_fingerprint.empty() &&
        account->revision_fingerprint != fingerprint)
      return safe_error("operation.conflict", 409);
    if (account->revision == revision && account->lifecycle == "logout_pending" &&
        account->operation_id != operation_id)
      return safe_error("operation.conflict", 409);
    const bool resuming = account->revision == revision && account->lifecycle == "logout_pending" &&
                          account->operation_id == operation_id;
    if (!resuming) {
      account->revision = revision;
      account->lifecycle = "logout_pending";
      account->operation_id = operation_id;
      account->operation_phase = "intent";
      account->revision_fingerprint = fingerprint;
      new_operation = true;
    } else {
      if (account->revision_fingerprint.empty())
        account->revision_fingerprint = fingerprint;
      if (account->operation_phase.empty()) {
        // Legacy manifests are an intent, never proof that logOut reached TDLib.
        account->operation_phase = "intent";
      }
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
  }

  const auto persist = [&](const std::string &phase, const std::string &lifecycle,
                           const std::string &persisted_operation) {
    AccountManifest manifest;
    nlohmann::json proxy;
    {
      std::lock_guard lock(mutex_);
      account->operation_phase = phase;
      manifest = {1,
                  uuid,
                  account->generation,
                  config_.use_test_dc,
                  1,
                  account->revision,
                  lifecycle,
                  persisted_operation,
                  false,
                  nullptr,
                  "",
                  ""};
      manifest.content_hash = account->revision_fingerprint;
      manifest.operation_phase = phase;
      proxy = account->proxy;
    }
    registry_.write(manifest, proxy);
  };

  try {
    if (new_operation)
      persist("intent", "logout_pending", operation_id);

    bool closed = false;
    std::string phase;
    {
      std::lock_guard lock(mutex_);
      closed = account->closed;
      phase = account->operation_phase;
    }
    if (closed && phase == "executing") {
      persist("confirmed", "logout_pending", operation_id);
      phase = "confirmed";
    }
    if (phase == "confirmed") {
      activate(*account);
      persist("", "active", "");
      std::lock_guard lock(mutex_);
      account->lifecycle = "active";
      account->operation_id.clear();
      account->operation_phase.clear();
      account->reconciled = true;
      account->busy = false;
      return {{"applied_revision", account->revision}, {"completed", true}};
    }

    activate(*account);
    persist("executing", "logout_pending", operation_id);
    std::int32_t client = 0;
    {
      std::lock_guard lock(mutex_);
      client = account->client_id;
    }
    const auto response = request(client, td_api::make_object<td_api::logOut>());
    if (td_error_code(response, 504)) {
      std::lock_guard lock(mutex_);
      account->busy = false;
      return safe_error("operation.outcome_unknown", 504);
    }
    if (!response || response->get_id() == td_api::error::ID) {
      std::lock_guard lock(mutex_);
      account->busy = false;
      return safe_error(td_error_message(response, "service.busy") ? "service.busy" : "telegram.operation_failed",
                        td_error_message(response, "service.busy") ? 503 : 502);
    }
    std::lock_guard lock(mutex_);
    account->busy = false;
    return {{"applied_revision", account->revision}, {"completed", false}};
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
}

nlohmann::json TdRuntime::update_proxy(const std::string &uuid, const nlohmann::json &command) {
  Account *account = nullptr;
  const std::string fingerprint = command_fingerprint(command);
  AccountManifest intent_manifest;
  nlohmann::json intent_proxy;
  {
    std::lock_guard lock(mutex_);
    account = &validated_account(uuid, command);
    if (account->revision == command.at("revision").get<std::uint64_t>() && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      const auto requested_proxy = command.value("proxy", nlohmann::json::object());
      if (!(requested_proxy.size() == 1 && requested_proxy.contains("id")) && account->proxy != requested_proxy)
        return safe_error("operation.conflict", 409);
      auto result = account_json(*account);
      result["completed"] = account->operation_id.empty();
      return result;
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    account->revision = command.at("revision").get<std::uint64_t>();
    account->effective_config_id = command.value("effective_config_id", "");
    account->proxy = command.value("proxy", nlohmann::json::object());
    account->operation_id = nullable_string(command, "operation_id");
    account->revision_fingerprint = fingerprint;
    intent_manifest = {1,
                       uuid,
                       account->generation,
                       config_.use_test_dc,
                       1,
                       account->revision,
                       account->lifecycle,
                       account->operation_id,
                       false,
                       nullptr,
                       "",
                       ""};
    intent_manifest.content_hash = account->revision_fingerprint;
    intent_proxy = account->proxy;
  }
  try {
    registry_.write(intent_manifest, intent_proxy);
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  try {
    close_account(*account, false);
    activate(*account);
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  AccountManifest completed;
  nlohmann::json proxy;
  nlohmann::json result;
  {
    std::lock_guard lock(mutex_);
    account->reconciled = true;
    account->busy = false;
    account->operation_id.clear();
    completed = intent_manifest;
    completed.operation_id.clear();
    proxy = account->proxy;
    result = account_json(*account);
  }
  registry_.write(completed, proxy);
  result["completed"] = true;
  return result;
}

nlohmann::json TdRuntime::ping_proxy(const std::string &uuid, const nlohmann::json &proxy_config) {
  std::int32_t client = 0;
  {
    std::lock_guard lock(mutex_);
    const auto found = accounts_.find(uuid);
    if (found == accounts_.end() || found->second.tombstone)
      return safe_error("account.not_found", 404);
    if (found->second.client_id == 0 || found->second.closed)
      return safe_error("service.busy", 503);
    client = found->second.client_id;
  }
  const auto mode = proxy_config.value("mode", "");
  const auto host = proxy_config.value("host", "");
  const auto port = proxy_config.value("port", 0);
  const auto username = proxy_config.value("username", "");
  const auto password = proxy_config.value("password", "");
  const auto secret = proxy_config.value("secret", "");
  const auto http_only = proxy_config.value("http_only", false);
  if (host.empty() || port < 1 || port > 65535 ||
      (mode == "mtproto" && (secret.empty() || !username.empty() || !password.empty())) ||
      ((mode == "socks5" || mode == "http") && !secret.empty()) ||
      (mode != "socks5" && mode != "http" && mode != "mtproto") || (!password.empty() && username.empty()) ||
      (mode != "http" && http_only))
    return safe_error("configuration.invalid", 422);
  td_api::object_ptr<td_api::ProxyType> type;
  if (mode == "socks5")
    type = td_api::make_object<td_api::proxyTypeSocks5>(username, password);
  else if (mode == "http")
    type = td_api::make_object<td_api::proxyTypeHttp>(username, password, http_only);
  else
    type = td_api::make_object<td_api::proxyTypeMtproto>(secret);
  auto proxy = td_api::make_object<td_api::proxy>(host, port, std::move(type));
  auto response = request(client, td_api::make_object<td_api::pingProxy>(std::move(proxy)));
  if (!response || response->get_id() != td_api::seconds::ID)
    return safe_error("proxy.unreachable", 502);
  const auto seconds = td::move_tl_object_as<td_api::seconds>(response);
  return {{"latency_ms", static_cast<std::int64_t>(std::llround(seconds->seconds_ * 1000.0))}};
}

nlohmann::json TdRuntime::remove(const std::string &uuid, const nlohmann::json &command) {
  Account *account = nullptr;
  const std::string fingerprint = command_fingerprint(command);
  const std::string operation_id = nullable_string(command, "operation_id");
  AccountManifest intent_manifest;
  nlohmann::json intent_proxy;
  bool new_operation = false;
  {
    std::lock_guard lock(mutex_);
    if (const auto existing = accounts_.find(uuid); existing != accounts_.end() && existing->second.tombstone) {
      const auto &removed = existing->second;
      if (removed.lifecycle == "removed" && command.value("generation", "") == removed.generation &&
          command.value("revision", 0ULL) == removed.revision && removed.revision_fingerprint == fingerprint) {
        return {{"applied_revision", removed.revision}, {"completed", true}};
      }
      return safe_error("account.gone", 410);
    }
    account = &validated_account(uuid, command);
    if (account->revision == command.at("revision").get<std::uint64_t>() && account->lifecycle == "removing" &&
        account->operation_id != operation_id)
      return safe_error("operation.conflict", 409);
    if (account->revision == command.at("revision").get<std::uint64_t>() && !account->revision_fingerprint.empty()) {
      if (account->revision_fingerprint != fingerprint)
        return safe_error("operation.conflict", 409);
      if (account->lifecycle == "removed")
        return {{"applied_revision", account->revision}, {"completed", true}};
      if (account->lifecycle != "removing" || account->operation_id != operation_id)
        return safe_error("operation.conflict", 409);
    }
    if (account->busy)
      return safe_error("operation.conflict", 409);
    account->busy = true;
    if (account->revision != command.at("revision").get<std::uint64_t>() || account->lifecycle != "removing") {
      account->revision = command.at("revision").get<std::uint64_t>();
      account->lifecycle = "removing";
      account->operation_id = operation_id;
      account->revision_fingerprint = fingerprint;
      account->operation_phase = "intent";
      new_operation = true;
    } else if (account->operation_phase.empty()) {
      account->operation_phase = "intent";
    }
    if (account->revision_fingerprint.empty())
      account->revision_fingerprint = fingerprint;
    intent_manifest = {1,
                       uuid,
                       account->generation,
                       config_.use_test_dc,
                       1,
                       account->revision,
                       account->lifecycle,
                       account->operation_id,
                       false,
                       nullptr,
                       "",
                       ""};
    intent_manifest.content_hash = account->revision_fingerprint;
    intent_manifest.operation_phase = account->operation_phase;
    intent_proxy = account->proxy;
  }
  try {
    if (new_operation)
      registry_.write(intent_manifest, intent_proxy);
    {
      std::lock_guard lock(mutex_);
      account->operation_phase = "closing";
      intent_manifest.operation_phase = "closing";
    }
    registry_.write(intent_manifest, intent_proxy);
    close_account(*account, true);
    registry_.remove_account_directory(uuid);
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  AccountManifest tombstone{1,
                            uuid,
                            account->generation,
                            config_.use_test_dc,
                            1,
                            account->revision,
                            "removed",
                            account->operation_id,
                            true,
                            nullptr,
                            "",
                            ""};
  tombstone.content_hash = account->revision_fingerprint;
  tombstone.operation_phase = "confirmed";
  try {
    registry_.write(tombstone, nlohmann::json{{"mode", "inherit"}, {"http_only", false}});
  } catch (...) {
    std::lock_guard lock(mutex_);
    account->busy = false;
    throw;
  }
  std::lock_guard lock(mutex_);
  account->closed = true;
  account->tombstone = true;
  account->reconciled = false;
  account->busy = false;
  account->lifecycle = "removed";
  account->operation_phase = "confirmed";
  return {{"applied_revision", account->revision}, {"completed", true}};
}

void TdRuntime::close_account(Account &account, bool destroy) {
  std::int32_t client = 0;
  {
    std::unique_lock lock(mutex_);
    account.interests.clear();
    account.interest_counts.clear();
    if (account.closing && !condition_.wait_for(lock, std::chrono::seconds(8), [&account] { return !account.closing; }))
      throw std::runtime_error("operation.outcome_unknown");
    if (account.client_id == 0 || account.closed)
      return;
    client = account.client_id;
  }
  td_api::object_ptr<td_api::Function> function =
      destroy ? td_api::object_ptr<td_api::Function>(td_api::make_object<td_api::destroy>())
              : td_api::object_ptr<td_api::Function>(td_api::make_object<td_api::close>());
  const auto response = request(client, std::move(function));
  if (td_error_code(response, 504))
    throw std::runtime_error("operation.outcome_unknown");
  if (!response || response->get_id() == td_api::error::ID)
    throw std::runtime_error("telegram.operation_failed");
  std::unique_lock lock(mutex_);
  if (!condition_.wait_for(lock, std::chrono::seconds(8), [&account] { return account.closed; }))
    throw std::runtime_error("operation.outcome_unknown");
  client_accounts_.erase(client);
  account.client_id = 0;
}

std::string TdRuntime::authorization_name(std::int32_t id) {
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

std::string TdRuntime::connection_name(std::int32_t id) {
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

std::vector<std::string> TdRuntime::allowed_actions(const Account &account) {
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

nlohmann::json TdRuntime::safe_error(const std::string &code, int status) {
  return {{"_error", true}, {"code", code}, {"status", status}};
}
} // namespace telebezel
