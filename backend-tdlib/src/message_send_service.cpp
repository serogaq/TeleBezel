#include "telebezel/parse.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <random>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace {
constexpr std::size_t operations_per_account = 256;
constexpr auto finished_retention = std::chrono::hours(24);
constexpr auto outcome_wait = std::chrono::seconds(5);
constexpr auto request_deadline = std::chrono::seconds(8);

struct SendFailure {
  std::string code;
  std::int64_t retry_after{0};
  bool retryable{false};
};

std::int64_t wait_seconds(const std::string &message) {
  for (const std::string marker : {"FLOOD_WAIT_", "SLOWMODE_WAIT_", "retry after "}) {
    const auto found = message.find(marker);
    if (found == std::string::npos)
      continue;
    auto end = found + marker.size();
    while (end < message.size() && message[end] >= '0' && message[end] <= '9')
      ++end;
    if (const auto seconds =
            parse_int64(std::string_view(message).substr(found + marker.size(), end - found - marker.size()));
        seconds && *seconds > 0)
      return std::min<std::int64_t>(*seconds, 86400);
  }
  return 0;
}

bool mentions(const std::string &message, std::initializer_list<const char *> markers) {
  return std::any_of(markers.begin(), markers.end(),
                     [&](const char *marker) { return message.find(marker) != std::string::npos; });
}

SendFailure send_failure(std::int32_t code, const std::string &message, double retry_after, bool can_retry) {
  const auto waited = std::max<std::int64_t>(wait_seconds(message), static_cast<std::int64_t>(std::ceil(retry_after)));
  if (code == 429 || waited > 0)
    return {"message.send_rate_limited", std::max<std::int64_t>(1, waited), true};
  if (mentions(message, {"MESSAGE_TOO_LONG", "too long"}))
    return {"message.text_too_long", 0, false};
  if (mentions(message, {"REPLY_MESSAGE", "replied", "reply"}))
    return {"message.reply_unavailable", 0, false};
  if (code == 403 || mentions(message, {"FORBIDDEN", "BANNED", "RESTRICTED", "USER_IS_BLOCKED", "YOU_BLOCKED_USER",
                                        "PRIVACY", "Have no rights", "have no write access", "can't write"}))
    return {"message.send_forbidden", 0, false};
  return {"message.send_failed", 0, can_retry || code >= 500};
}

std::int32_t new_sending_id(const Account &account) {
  thread_local std::mt19937 engine{std::random_device{}()};
  std::uniform_int_distribution<std::int32_t> distribution(1, std::numeric_limits<std::int32_t>::max());
  while (true) {
    const auto candidate = distribution(engine);
    if (!account.sending_ids.contains(candidate))
      return candidate;
  }
}

std::string public_state(const std::string &state) {
  if (state == "sent" || state == "failed" || state == "unknown")
    return state;
  return "pending";
}

void prune(Account &account, std::chrono::steady_clock::time_point now) {
  for (auto item = account.sends.begin(); item != account.sends.end();) {
    const auto &operation = item->second;
    const auto finished = operation.finished != std::chrono::steady_clock::time_point{};
    if (now - (finished ? operation.finished : operation.created) < finished_retention) {
      ++item;
      continue;
    }
    account.sending_ids.erase(operation.sending_id);
    if (operation.temporary_id != 0)
      account.send_messages.erase({operation.chat_id, operation.temporary_id});
    item = account.sends.erase(item);
  }
}

td_api::object_ptr<td_api::sendMessage> send_request(const SendCommand &command, std::int32_t sending_id) {
  auto request = td_api::make_object<td_api::sendMessage>();
  request->chat_id_ = command.chat_id;
  if (command.reply_to != 0) {
    auto reply = td_api::make_object<td_api::inputMessageReplyToMessage>();
    reply->message_id_ = command.reply_to;
    request->reply_to_ = std::move(reply);
  }
  request->options_ = td_api::make_object<td_api::messageSendOptions>();
  request->options_->sending_id_ = sending_id;
  auto content = td_api::make_object<td_api::inputMessageText>();
  content->text_ = td_api::make_object<td_api::formattedText>();
  content->text_->text_ = command.text;
  content->clear_draft_ = false;
  request->input_message_content_ = std::move(content);
  return request;
}

std::int64_t reply_target(const td_api::message &message) {
  if (!message.reply_to_ || message.reply_to_->get_id() != td_api::messageReplyToMessage::ID)
    return 0;
  return static_cast<const td_api::messageReplyToMessage &>(*message.reply_to_).message_id_;
}

void replace_projection(AccountStore &store, Account &account, std::int64_t chat_id, std::int64_t old_id,
                        const td_api::message &message) {
  if (account.chats.find(chat_id) == account.chats.end())
    return;
  if (old_id != message.id_) {
    store.erase_message(account, {chat_id, old_id});
    UpdateJournal::append(store, account, "message_deleted", chat_id, old_id);
  }
  auto projection = message_projection(message);
  decorate_sender(account, projection);
  store.put_message(account, {chat_id, message.id_}, std::move(projection));
  UpdateJournal::append(store, account, "message_changed", chat_id, message.id_);
  auto &chat = account.chats.at(chat_id);
  auto &last = chat["last_message"];
  if (last.is_object() && last.value("id", "") == std::to_string(old_id)) {
    last = message_projection(message);
    UpdateJournal::append(store, account, "chat_changed", chat_id, 0, "last_message");
  }
}
} // namespace

nlohmann::json MessageSendService::operation_json(const std::string &id, const SendOperation &operation) {
  nlohmann::json result{{"operation_id", id},
                        {"state", public_state(operation.state)},
                        {"chat_id", std::to_string(operation.chat_id)},
                        {"retryable", operation.retryable},
                        {"reply_dropped", operation.reply_dropped},
                        {"error", nullptr}};
  if (operation.temporary_id != 0)
    result["temporary_message_id"] = std::to_string(operation.temporary_id);
  if (operation.message_id != 0)
    result["message_id"] = std::to_string(operation.message_id);
  if (!operation.error.empty()) {
    result["error"] = {{"code", operation.error}};
    if (operation.retry_after > 0)
      result["error"]["retry_after"] = operation.retry_after;
  }
  return result;
}

void MessageSendService::publish(AccountStore &store, Account &account, const std::string &id,
                                 const SendOperation &operation) {
  auto event = operation_json(id, operation);
  event.erase("temporary_message_id");
  event["type"] = "send_changed";
  UpdateJournal::append_event(store, account, std::move(event));
}

void MessageSendService::finish(AccountStore &store, Account &account, const std::string &id, const std::string &error,
                                std::int64_t retry_after, bool retryable) {
  const auto found = account.sends.find(id);
  if (found == account.sends.end() || found->second.state == "sent" || found->second.state == "failed")
    return;
  auto &operation = found->second;
  operation.state = "failed";
  operation.error = error;
  operation.retry_after = retry_after;
  operation.retryable = retryable;
  operation.finished = std::chrono::steady_clock::now();
  account.sending_ids.erase(operation.sending_id);
  publish(store, account, id, operation);
}

nlohmann::json MessageSendService::send(const std::string &uuid, const SendCommand &command) {
  const auto started = std::chrono::steady_clock::now();
  const auto deadline = started + request_deadline;
  std::int32_t client = 0;
  std::int32_t sending_id = 0;
  bool created = false;
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end() || found->second.tombstone)
      throw std::runtime_error("account.not_found");
    auto &account = found->second;
    if (account.generation != command.generation)
      throw std::runtime_error("storage.identity_mismatch");
    const auto fence = read_fence(account);
    if (!fence)
      return safe_error("authorization.invalid_state", 409);
    if (account.authorization_generation != command.authorization_generation)
      throw std::runtime_error("operation.conflict");
    client = fence->client;
    prune(account, started);
    if (const auto existing = account.sends.find(command.operation_id); existing != account.sends.end()) {
      if (existing->second.chat_id != command.chat_id || existing->second.reply_to != command.reply_to)
        throw std::runtime_error("operation.conflict");
    } else {
      if (!account.chats.contains(command.chat_id))
        throw std::runtime_error("chat.not_found");
      if (account.sends.size() >= operations_per_account)
        throw std::runtime_error("service.busy");
      SendOperation operation;
      operation.chat_id = command.chat_id;
      operation.reply_to = command.reply_to;
      operation.sending_id = sending_id = new_sending_id(account);
      operation.epoch = account.runtime_epoch;
      operation.authorization_generation = account.authorization_generation;
      account.sending_ids.emplace(sending_id, command.operation_id);
      account.sends.emplace(command.operation_id, std::move(operation));
      created = true;
    }
  }
  if (created)
    dispatch(uuid, command, client, sending_id, deadline);
  return await_outcome(uuid, command.operation_id, std::min(deadline, started + outcome_wait));
}

void MessageSendService::dispatch(const std::string &uuid, const SendCommand &command, std::int32_t client,
                                  std::int32_t sending_id, std::chrono::steady_clock::time_point deadline) {
  const auto fail = [&](const std::string &code, bool retryable) {
    std::lock_guard lock(context_.mutex_);
    if (const auto found = context_.accounts_.find(uuid); found != context_.accounts_.end())
      finish(context_, found->second, command.operation_id, code, 0, retryable);
  };
  const auto budget = [&](std::chrono::milliseconds cap) {
    return std::min(cap,
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
  };
  if (command.reply_to != 0) {
    auto original = broker_.request(client, td_api::make_object<td_api::getMessage>(command.chat_id, command.reply_to),
                                    budget(std::chrono::seconds(2)));
    if (!original || original->get_id() != td_api::message::ID) {
      fail(td_error_code(original, 504) || td_error_message(original, "service.busy") ? "message.send_failed"
                                                                                      : "message.reply_unavailable",
           td_error_code(original, 504));
      return;
    }
    auto properties =
        broker_.request(client, td_api::make_object<td_api::getMessageProperties>(command.chat_id, command.reply_to),
                        budget(std::chrono::seconds(2)));
    if (!properties || properties->get_id() != td_api::messageProperties::ID) {
      fail(td_error_code(properties, 504) ? "message.send_failed" : "message.reply_unavailable",
           td_error_code(properties, 504));
      return;
    }
    if (!static_cast<const td_api::messageProperties &>(*properties).can_be_replied_) {
      fail("message.reply_unavailable", false);
      return;
    }
  }
  {
    std::lock_guard lock(context_.mutex_);
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end())
      return;
    const auto operation = found->second.sends.find(command.operation_id);
    if (operation == found->second.sends.end() || operation->second.state != "preparing")
      return;
    operation->second.state = "dispatching";
  }
  RequestHandle handle;
  try {
    handle = broker_.begin_request(client, send_request(command, sending_id));
  } catch (const std::exception &) {
    fail("message.send_failed", true);
    return;
  }
  const bool reached = handle.pending;
  auto response = broker_.await_request(std::move(handle), budget(std::chrono::seconds(3)));
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end())
    return;
  auto &account = found->second;
  const auto operation = account.sends.find(command.operation_id);
  if (operation == account.sends.end())
    return;
  if (response && response->get_id() == td_api::message::ID) {
    const auto &message = static_cast<const td_api::message &>(*response);
    if (operation->second.temporary_id == 0 && message.sending_state_) {
      operation->second.temporary_id = message.id_;
      account.send_messages[{message.chat_id_, message.id_}] = command.operation_id;
    }
    if (operation->second.state == "dispatching") {
      if (message.sending_state_) {
        operation->second.state = "pending";
        publish(context_, account, command.operation_id, operation->second);
      } else {
        operation->second.state = "sent";
        operation->second.message_id = message.id_;
        operation->second.reply_dropped = command.reply_to != 0 && reply_target(message) != command.reply_to;
        operation->second.finished = std::chrono::steady_clock::now();
        account.sending_ids.erase(sending_id);
        publish(context_, account, command.operation_id, operation->second);
      }
    }
    return;
  }
  if (!reached || (response && response->get_id() == td_api::error::ID && !td_error_code(response, 504))) {
    const auto *error = response && response->get_id() == td_api::error::ID
                            ? static_cast<const td_api::error *>(response.get())
                            : nullptr;
    const auto failure = error == nullptr ? SendFailure{"message.send_failed", 0, true}
                                          : send_failure(error->code_, error->message_, 0, false);
    if (!reached || td_error_message(response, "service.busy") || td_error_message(response, "service.stopping"))
      finish(context_, account, command.operation_id, "message.send_failed", 0, true);
    else
      finish(context_, account, command.operation_id, failure.code, failure.retry_after, failure.retryable);
  }
}

nlohmann::json MessageSendService::await_outcome(const std::string &uuid, const std::string &id,
                                                 std::chrono::steady_clock::time_point until) {
  std::unique_lock lock(context_.mutex_);
  while (true) {
    const auto found = context_.accounts_.find(uuid);
    if (found == context_.accounts_.end())
      throw std::runtime_error("account.not_found");
    const auto operation = found->second.sends.find(id);
    if (operation == found->second.sends.end())
      return {{"operation_id", id},     {"state", "unknown"}, {"retryable", false},
              {"reply_dropped", false}, {"error", nullptr},   {"runtime_epoch", found->second.runtime_epoch}};
    const auto &state = operation->second.state;
    if (state == "sent" || state == "failed" || std::chrono::steady_clock::now() >= until) {
      auto result = operation_json(id, operation->second);
      result["runtime_epoch"] = found->second.runtime_epoch;
      return result;
    }
    context_.condition_.wait_until(lock, until);
  }
}

nlohmann::json MessageSendService::lookup(const std::string &uuid, const std::vector<std::string> &ids) const {
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end() || found->second.tombstone)
    throw std::runtime_error("account.not_found");
  const auto &account = found->second;
  nlohmann::json operations = nlohmann::json::object();
  for (const auto &id : ids) {
    const auto operation = account.sends.find(id);
    if (operation == account.sends.end() || operation->second.epoch != account.runtime_epoch ||
        operation->second.authorization_generation != account.authorization_generation)
      operations[id] = {{"operation_id", id}, {"state", "not_found"}};
    else
      operations[id] = operation_json(id, operation->second);
  }
  return {{"operations", operations}, {"runtime_epoch", account.runtime_epoch}};
}

void MessageSendService::bind_pending(AccountStore &store, Account &account, const td_api::message &message) {
  if (!message.sending_state_ || message.sending_state_->get_id() != td_api::messageSendingStatePending::ID)
    return;
  const auto sending_id = static_cast<const td_api::messageSendingStatePending &>(*message.sending_state_).sending_id_;
  const auto owner = account.sending_ids.find(sending_id);
  if (sending_id == 0 || owner == account.sending_ids.end())
    return;
  const auto operation = account.sends.find(owner->second);
  if (operation == account.sends.end() || operation->second.chat_id != message.chat_id_)
    return;
  if (operation->second.temporary_id == 0) {
    operation->second.temporary_id = message.id_;
    account.send_messages[{message.chat_id_, message.id_}] = owner->second;
  }
  if (operation->second.state == "dispatching" || operation->second.state == "preparing") {
    operation->second.state = "pending";
    publish(store, account, owner->second, operation->second);
  }
}

void MessageSendService::succeeded(AccountStore &store, Account &account,
                                   const td_api::updateMessageSendSucceeded &update) {
  if (!update.message_)
    return;
  const auto &message = *update.message_;
  replace_projection(store, account, message.chat_id_, update.old_message_id_, message);
  const auto owner = account.send_messages.find({message.chat_id_, update.old_message_id_});
  if (owner == account.send_messages.end())
    return;
  const auto id = owner->second;
  account.send_messages.erase(owner);
  const auto operation = account.sends.find(id);
  if (operation == account.sends.end() || operation->second.state == "sent" || operation->second.state == "failed")
    return;
  operation->second.state = "sent";
  operation->second.message_id = message.id_;
  operation->second.reply_dropped =
      operation->second.reply_to != 0 && reply_target(message) != operation->second.reply_to;
  operation->second.finished = std::chrono::steady_clock::now();
  account.sending_ids.erase(operation->second.sending_id);
  publish(store, account, id, operation->second);
}

void MessageSendService::failed(AccountStore &store, Account &account, const td_api::updateMessageSendFailed &update) {
  if (!update.message_)
    return;
  const auto &message = *update.message_;
  replace_projection(store, account, message.chat_id_, update.old_message_id_, message);
  const auto owner = account.send_messages.find({message.chat_id_, update.old_message_id_});
  if (owner == account.send_messages.end())
    return;
  const auto id = owner->second;
  account.send_messages.erase(owner);
  bool can_retry = false;
  double retry_after = 0;
  bool drop_reply = false;
  if (message.sending_state_ && message.sending_state_->get_id() == td_api::messageSendingStateFailed::ID) {
    const auto &state = static_cast<const td_api::messageSendingStateFailed &>(*message.sending_state_);
    can_retry = state.can_retry_;
    retry_after = state.retry_after_;
    drop_reply = state.need_drop_reply_;
  }
  auto failure = update.error_ ? send_failure(update.error_->code_, update.error_->message_, retry_after, can_retry)
                               : SendFailure{"message.send_failed", 0, can_retry};
  if (drop_reply)
    failure = {"message.reply_unavailable", 0, false};
  const auto operation = account.sends.find(id);
  if (operation != account.sends.end())
    operation->second.message_id = message.id_;
  finish(store, account, id, failure.code, failure.retry_after, failure.retryable);
}
} // namespace telebezel::runtime
