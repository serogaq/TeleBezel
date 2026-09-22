#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace {
std::optional<std::int64_t> update_chat_id(const td_api::Object &object) {
  switch (object.get_id()) {
  case td_api::updateChatTitle::ID:
    return static_cast<const td_api::updateChatTitle &>(object).chat_id_;
  case td_api::updateChatPosition::ID:
    return static_cast<const td_api::updateChatPosition &>(object).chat_id_;
  case td_api::updateChatLastMessage::ID:
    return static_cast<const td_api::updateChatLastMessage &>(object).chat_id_;
  case td_api::updateChatDraftMessage::ID:
    return static_cast<const td_api::updateChatDraftMessage &>(object).chat_id_;
  case td_api::updateNewMessage::ID: {
    const auto &update = static_cast<const td_api::updateNewMessage &>(object);
    return update.message_ ? std::optional<std::int64_t>(update.message_->chat_id_) : std::nullopt;
  }
  case td_api::updateMessageContent::ID:
    return static_cast<const td_api::updateMessageContent &>(object).chat_id_;
  case td_api::updateMessageEdited::ID:
    return static_cast<const td_api::updateMessageEdited &>(object).chat_id_;
  case td_api::updateDeleteMessages::ID:
    return static_cast<const td_api::updateDeleteMessages &>(object).chat_id_;
  case td_api::updateChatReadInbox::ID:
    return static_cast<const td_api::updateChatReadInbox &>(object).chat_id_;
  case td_api::updateChatReadOutbox::ID:
    return static_cast<const td_api::updateChatReadOutbox &>(object).chat_id_;
  case td_api::updateChatIsMarkedAsUnread::ID:
    return static_cast<const td_api::updateChatIsMarkedAsUnread &>(object).chat_id_;
  default:
    return std::nullopt;
  }
}

void report_unknown_chat(const std::string &uuid, std::int64_t chat_id, std::int32_t update_id) {
  std::cerr << nlohmann::json{{"event", "chat_projection_missing"},
                              {"account_uuid", uuid},
                              {"chat_id", std::to_string(chat_id)},
                              {"update_id", update_id}}
                   .dump()
            << '\n';
}
} // namespace
void RuntimeEngine::start() {
  if (receive_thread_.joinable())
    return;
  const auto version = transport_.initialize();
  {
    std::lock_guard lock(context_.mutex_);
    context_.status_.ready = true;
    context_.status_.tdlib_version = version;
    for (const auto &manifest : registry_.discover()) {
      Account account;
      account.uuid = manifest.uuid;
      account.generation = manifest.generation;
      account.revision = manifest.revision;
      account.applied_revision = manifest.applied_revision;
      account.authorization_generation = manifest.authorization_generation;
      account.use_test_dc = manifest.use_test_dc;
      account.lifecycle = manifest.lifecycle;
      account.operation_id = manifest.operation_id;
      account.operation_phase = manifest.operation_phase;
      account.revision_fingerprint =
          manifest.content_hash.starts_with(fingerprint_version) ? manifest.content_hash : std::string{};
      account.proxy = manifest.proxy;
      account.runtime_epoch = make_request_id();
      account.tombstone = manifest.tombstone;
      account.closed = manifest.tombstone;
      context_.accounts_.emplace(account.uuid, std::move(account));
      std::cerr << nlohmann::json{{"event", "account_awaiting_reconciliation"},
                                  {"account_uuid", manifest.uuid},
                                  {"reason", "reconciliation_not_received"}}
                       .dump()
                << '\n';
    }
    for (const auto &uuid : registry_.orphan_account_ids()) {
      context_.orphan_accounts_.insert(uuid);
      std::cerr << nlohmann::json{{"event", "account_storage_unregistered"},
                                  {"account_uuid", uuid},
                                  {"reason", "reconciliation_not_received"}}
                       .dump()
                << '\n';
    }
    context_.manifest_errors_ = registry_.manifest_errors();
    for (const auto &[uuid, error] : context_.manifest_errors_) {
      std::cerr
          << nlohmann::json{{"event", "account_storage_error"}, {"account_uuid", uuid}, {"error_code", error}}.dump()
          << '\n';
    }
    context_.status_.account_count = context_.accounts_.size();
  }
  broker_.start();
  receive_done_.store(false);
  {
    std::lock_guard lock(authorization_mutex_);
    authorization_stopping_ = false;
  }
  authorization_thread_ = std::thread(&RuntimeEngine::persist_authorizations, this);
  receive_thread_ = std::thread(&RuntimeEngine::receive_loop, this);
}

void RuntimeEngine::stop() {
  if (!receive_thread_.joinable())
    return;
  broker_.stop();
  std::vector<std::int32_t> clients;
  {
    std::lock_guard lock(context_.mutex_);
    context_.status_.ready = false;
    for (const auto &[uuid, account] : context_.accounts_) {
      static_cast<void>(uuid);
      if (account.client_id != 0 && !account.closed)
        clients.push_back(account.client_id);
    }
  }
  for (const auto client : clients) {
    transport_.send(client, broker_.next_id(), td_api::make_object<td_api::close>());
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  {
    std::unique_lock lock(context_.mutex_);
    context_.condition_.wait_until(lock, deadline, [this] {
      return std::none_of(context_.accounts_.begin(), context_.accounts_.end(),
                          [](const auto &entry) { return entry.second.client_id != 0 && !entry.second.closed; });
    });
  }
  if (std::chrono::steady_clock::now() >= deadline) {
    std::cerr << nlohmann::json{{"event", "runtime_shutdown_timeout"}}.dump() << '\n';
  }
  receive_done_.store(true);
  receive_thread_.join();
  {
    std::lock_guard lock(authorization_mutex_);
    authorization_stopping_ = true;
  }
  authorization_condition_.notify_all();
  authorization_thread_.join();
}

void RuntimeEngine::persist_authorizations() {
  while (true) {
    std::pair<std::string, std::uint64_t> job;
    {
      std::unique_lock lock(authorization_mutex_);
      authorization_condition_.wait(lock, [this] { return authorization_stopping_ || !authorization_queue_.empty(); });
      if (authorization_queue_.empty())
        return;
      job = std::move(authorization_queue_.front());
      authorization_queue_.pop_front();
    }
    try {
      registry_.persist_authorization_generation(job.first, job.second);
    } catch (const std::exception &) {
      std::lock_guard lock(context_.mutex_);
      if (const auto found = context_.accounts_.find(job.first);
          found != context_.accounts_.end() && found->second.authorization_generation == job.second) {
        found->second.reconciled = false;
        found->second.last_error = "storage.io_error";
      }
      std::cerr << nlohmann::json{{"event", "authorization_generation_persist_failed"},
                                  {"account_uuid", job.first},
                                  {"error_code", "storage.io_error"}}
                       .dump()
                << '\n';
    }
  }
}

StatusSnapshot RuntimeEngine::status() const {
  std::lock_guard lock(context_.mutex_);
  return context_.status_;
}

nlohmann::json RuntimeEngine::snapshots(const std::vector<std::string> &ids) const {
  std::lock_guard lock(context_.mutex_);
  nlohmann::json result = nlohmann::json::object();
  if (ids.empty()) {
    for (const auto &[id, account] : context_.accounts_) {
      result[id] = account_json(account);
    }
  } else {
    for (const auto &id : ids) {
      if (const auto iterator = context_.accounts_.find(id); iterator != context_.accounts_.end())
        result[id] = account_json(iterator->second);
    }
  }
  return {{"accounts", result},
          {"unregistered_storage", context_.orphan_accounts_},
          {"manifest_errors", context_.manifest_errors_}};
}

nlohmann::json RuntimeEngine::snapshot(const std::string &uuid) const {
  std::lock_guard lock(context_.mutex_);
  const auto iterator = context_.accounts_.find(uuid);
  return iterator == context_.accounts_.end() ? safe_error("account.not_found", 404) : account_json(iterator->second);
}

void RuntimeEngine::receive_loop() {
  auto next_reminder = std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (!receive_done_.load()) {
    const auto stalled_clients = broker_.expire(std::chrono::steady_clock::now());
    for (const auto client : stalled_clients) {
      transport_.send(client, broker_.next_id(), td_api::make_object<td_api::close>());
    }
    auto response = transport_.receive(0.1);
    if (!response.object) {
      if (std::chrono::steady_clock::now() >= next_reminder) {
        std::lock_guard lock(context_.mutex_);
        for (const auto &[uuid, account] : context_.accounts_) {
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
    std::lock_guard lock(context_.mutex_);
    if (response.request_id == 0) {
      handle_update(response.client_id, *response.object);
      continue;
    }
    if (broker_.complete(response) && response.object->get_id() == td_api::user::ID) {
      const auto mapped = context_.client_accounts_.find(response.client_id);
      if (mapped != context_.client_accounts_.end()) {
        const auto &user = static_cast<const td_api::user &>(*response.object);
        std::vector<std::string> usernames;
        if (user.usernames_)
          usernames = user.usernames_->active_usernames_;
        context_.accounts_.at(mapped->second).telegram_identity = {{"id", std::to_string(user.id_)},
                                                                   {"first_name", user.first_name_},
                                                                   {"last_name", user.last_name_},
                                                                   {"usernames", usernames},
                                                                   {"is_premium", user.is_premium_}};
      }
    }
  }
}

void RuntimeEngine::handle_update(std::int32_t client_id, td_api::Object &object) {
  const auto mapped = context_.client_accounts_.find(client_id);
  if (mapped == context_.client_accounts_.end())
    return;
  auto &account = context_.accounts_.at(mapped->second);
  if (object.get_id() == td_api::updateAuthorizationState::ID) {
    auto &update = static_cast<td_api::updateAuthorizationState &>(object);
    const auto state_id = update.authorization_state_ ? update.authorization_state_->get_id() : 0;
    const bool leaving_authorized_session =
        account.authorization_state == "ready" && state_id != td_api::authorizationStateReady::ID;
    if (leaving_authorized_session) {
      if (account.lifecycle == "active" && !account.busy && !account.closing) {
        ++account.authorization_generation;
        {
          std::lock_guard lock(authorization_mutex_);
          authorization_queue_.emplace_back(account.uuid, account.authorization_generation);
        }
        authorization_condition_.notify_one();
      }
      account.chats.clear();
      context_.clear_messages(account);
      account.sender_names.clear();
      account.events.clear();
      account.interests.clear();
      account.interest_counts.clear();
      account.interest_states.clear();
      account.telegram_identity = nullptr;
      account.main_exhausted = false;
      account.archive_exhausted = false;
      account.main_orders.reset();
      account.archive_orders.reset();
      account.reconciled = false;
    }
    account.authorization_state = authorization_name(state_id);
    account.authorization_version = std::to_string(broker_.next_id());
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
      account.proxy_applied = false;
      account.applied_proxy = nullptr;
      account.applied_telegram_api_id = 0;
      account.applied_telegram_api_hash.clear();
      context_.client_accounts_.erase(mapped);
      if (account.lifecycle != "logout_pending" && account.lifecycle != "removing")
        account.reconciled = false;
      context_.condition_.notify_all();
    } else if (state_id == td_api::authorizationStateReady::ID) {
      if (const auto error = broker_.request_identity(account.client_id))
        account.last_error = *error;
    }
  } else if (object.get_id() == td_api::updateConnectionState::ID) {
    auto &update = static_cast<td_api::updateConnectionState &>(object);
    account.connection_state = update.state_ ? connection_name(update.state_->get_id()) : "unknown";
  } else if (object.get_id() == td_api::updateUser::ID) {
    const auto &update = static_cast<td_api::updateUser &>(object);
    if (update.user_) {
      const auto key = "user:" + std::to_string(update.user_->id_);
      auto name = update.user_->first_name_;
      if (!update.user_->last_name_.empty()) {
        if (!name.empty())
          name += " ";
        name += update.user_->last_name_;
      }
      if (!name.empty()) {
        account.sender_names[key] = name;
      }
    }
  } else if (object.get_id() == td_api::updateNewChat::ID) {
    auto &update = static_cast<td_api::updateNewChat &>(object);
    if (update.chat_) {
      auto projection = chat_projection(*update.chat_);
      auto *const known = existing_chat(account, update.chat_->id_);
      record_order_change(
          account, known == nullptr ? nlohmann::json::object() : known->value("positions", nlohmann::json::object()),
          projection.value("positions", nlohmann::json::object()));
      account.chats[update.chat_->id_] = std::move(projection);
      account.sender_names["chat:" + std::to_string(update.chat_->id_)] = update.chat_->title_;
      if (update.chat_->last_message_) {
        auto message = message_projection(*update.chat_->last_message_);
        decorate_sender(account, message);
        context_.put_message(account, {update.chat_->id_, update.chat_->last_message_->id_}, std::move(message));
      }
      UpdateJournal::append(account, "chat_changed", update.chat_->id_);
    }
    return;
  }
  // Every remaining update addresses a chat TDLib already announced with
  // updateNewChat; materialising one here would publish a projection holding
  // nothing but the fields of this update.
  const auto chat_id = update_chat_id(object);
  if (!chat_id)
    return;
  auto *const chat = existing_chat(account, *chat_id);
  if (chat == nullptr) {
    report_unknown_chat(account.uuid, *chat_id, object.get_id());
    return;
  }
  if (object.get_id() == td_api::updateChatTitle::ID) {
    auto &update = static_cast<td_api::updateChatTitle &>(object);
    (*chat)["title"] = update.title_;
    account.sender_names["chat:" + std::to_string(update.chat_id_)] = update.title_;
    UpdateJournal::append(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatPosition::ID) {
    auto &update = static_cast<td_api::updateChatPosition &>(object);
    const auto before = chat->value("positions", nlohmann::json::object());
    if (!chat->contains("positions"))
      (*chat)["positions"] = nlohmann::json::object();
    apply_position(*chat, update.position_.get());
    record_order_change(account, before, chat->value("positions", nlohmann::json::object()));
    UpdateJournal::append(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatLastMessage::ID) {
    auto &update = static_cast<td_api::updateChatLastMessage &>(object);
    const auto before = chat->value("positions", nlohmann::json::object());
    (*chat)["positions"] = nlohmann::json::object();
    for (const auto &position : update.positions_)
      apply_position(*chat, position.get());
    record_order_change(account, before, chat->value("positions", nlohmann::json::object()));
    (*chat)["last_message"] =
        update.last_message_ ? message_projection(*update.last_message_) : nlohmann::json(nullptr);
    if (update.last_message_) {
      auto projection = message_projection(*update.last_message_);
      decorate_sender(account, projection);
      context_.put_message(account, {update.chat_id_, update.last_message_->id_}, std::move(projection));
    }
    UpdateJournal::append(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatDraftMessage::ID) {
    auto &update = static_cast<td_api::updateChatDraftMessage &>(object);
    const auto before = chat->value("positions", nlohmann::json::object());
    (*chat)["positions"] = nlohmann::json::object();
    for (const auto &position : update.positions_)
      apply_position(*chat, position.get());
    record_order_change(account, before, chat->value("positions", nlohmann::json::object()));
    UpdateJournal::append(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateNewMessage::ID) {
    auto &update = static_cast<td_api::updateNewMessage &>(object);
    if (update.message_) {
      auto projection = message_projection(*update.message_);
      decorate_sender(account, projection);
      context_.put_message(account, {update.message_->chat_id_, update.message_->id_}, std::move(projection));
      UpdateJournal::append(account, "message_changed", update.message_->chat_id_, update.message_->id_);
    }
  } else if (object.get_id() == td_api::updateMessageContent::ID) {
    auto &update = static_cast<td_api::updateMessageContent &>(object);
    auto found = account.messages.find({update.chat_id_, update.message_id_});
    if (found != account.messages.end()) {
      auto projection = found->second;
      projection["content"] = message_content(update.new_content_.get());
      context_.put_message(account, {update.chat_id_, update.message_id_}, std::move(projection));
    }
    auto &last = (*chat)["last_message"];
    if (last.is_object() && last.value("id", "") == std::to_string(update.message_id_)) {
      last["content"] = message_content(update.new_content_.get());
      UpdateJournal::append(account, "chat_changed", update.chat_id_);
    }
    UpdateJournal::append(account, "message_changed", update.chat_id_, update.message_id_);
  } else if (object.get_id() == td_api::updateMessageEdited::ID) {
    auto &update = static_cast<td_api::updateMessageEdited &>(object);
    auto found = account.messages.find({update.chat_id_, update.message_id_});
    if (found != account.messages.end()) {
      auto projection = found->second;
      projection["edit_date"] = update.edit_date_;
      context_.put_message(account, {update.chat_id_, update.message_id_}, std::move(projection));
    }
    auto &last = (*chat)["last_message"];
    if (last.is_object() && last.value("id", "") == std::to_string(update.message_id_)) {
      last["edit_date"] = update.edit_date_;
      UpdateJournal::append(account, "chat_changed", update.chat_id_);
    }
    UpdateJournal::append(account, "message_changed", update.chat_id_, update.message_id_);
  } else if (object.get_id() == td_api::updateDeleteMessages::ID) {
    auto &update = static_cast<td_api::updateDeleteMessages &>(object);
    for (const auto message_id : update.message_ids_) {
      context_.erase_message(account, {update.chat_id_, message_id});
      UpdateJournal::append(account, update.from_cache_ ? "message_changed" : "message_deleted", update.chat_id_,
                            message_id);
      auto &last = (*chat)["last_message"];
      if (last.is_object() && last.value("id", "") == std::to_string(message_id)) {
        last = nullptr;
        UpdateJournal::append(account, "chat_changed", update.chat_id_);
      }
    }
  } else if (object.get_id() == td_api::updateChatReadInbox::ID) {
    auto &update = static_cast<td_api::updateChatReadInbox &>(object);
    (*chat)["last_read_inbox_message_id"] = std::to_string(update.last_read_inbox_message_id_);
    (*chat)["unread_count"] = update.unread_count_;
    UpdateJournal::append(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatReadOutbox::ID) {
    auto &update = static_cast<td_api::updateChatReadOutbox &>(object);
    (*chat)["last_read_outbox_message_id"] = std::to_string(update.last_read_outbox_message_id_);
    UpdateJournal::append(account, "chat_changed", update.chat_id_);
  } else if (object.get_id() == td_api::updateChatIsMarkedAsUnread::ID) {
    auto &update = static_cast<td_api::updateChatIsMarkedAsUnread &>(object);
    (*chat)["is_marked_unread"] = update.is_marked_as_unread_;
    UpdateJournal::append(account, "chat_changed", update.chat_id_);
  }
}

} // namespace telebezel::runtime
