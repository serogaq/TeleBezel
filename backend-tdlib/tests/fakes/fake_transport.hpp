#pragma once
#include "telebezel/transport.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <td/telegram/td_api.h>
#include <vector>

namespace telebezel::testing {
namespace td_api = td::td_api;
class FakeTransport final : public telebezel::TdTransport {
public:
  std::string initialize() override { return "1.8.67"; }
  std::int32_t create_client_id() override { return next_client_++; }
  void send(std::int32_t client, std::uint64_t request, td_api::object_ptr<td_api::Function> function) override {
    if (fail_send_.exchange(false))
      throw std::runtime_error("service.tdlib_unavailable");
    const auto type = function->get_id();
    {
      std::lock_guard lock(mutex_);
      sent_.emplace_back(client, type);
      if (type == td_api::checkAuthenticationCode::ID)
        last_code_ = static_cast<const td_api::checkAuthenticationCode &>(*function).code_;
    }
    td_api::object_ptr<td_api::Object> scripted;
    bool has_script = false;
    {
      std::lock_guard lock(mutex_);
      auto &queue = scripted_[type];
      if (!queue.empty()) {
        has_script = true;
        scripted = std::move(queue.front());
        queue.pop_front();
      }
    }
    if (has_script) {
      if (scripted)
        push({client, request, std::move(scripted)});
      return;
    }
    if (type == td_api::getMe::ID) {
      auto user = td_api::make_object<td_api::user>();
      user->id_ = 9007199254740000LL;
      user->first_name_ = "Ada";
      user->last_name_ = "Lovelace";
      user->phone_number_ = "+15550000000";
      user->is_premium_ = true;
      user->usernames_ = td_api::make_object<td_api::usernames>();
      user->usernames_->active_usernames_ = {"ada"};
      push({client, request, std::move(user)});
    } else if (type == td_api::getProxies::ID) {
      push({client, request, td_api::make_object<td_api::addedProxies>()});
    } else if (type == td_api::addProxy::ID) {
      if (fail_add_proxy_.load())
        push({client, request, td_api::make_object<td_api::error>(400, "PROXY_FAILED")});
      else
        push({client, request, td_api::make_object<td_api::addedProxy>()});
    } else if (type == td_api::pingProxy::ID) {
      push({client, request, td_api::make_object<td_api::seconds>(0.125)});
    } else {
      push({client, request, td_api::make_object<td_api::ok>()});
    }
    if (type == td_api::setTdlibParameters::ID) {
      push({client, 0,
            td_api::make_object<td_api::updateAuthorizationState>(
                td_api::make_object<td_api::authorizationStateWaitPhoneNumber>())});
    } else if ((type == td_api::close::ID || type == td_api::destroy::ID) && suppress_close_updates_.load()) {
      // Tests release the storage owner explicitly with authorizationStateClosed.
    } else if (type == td_api::close::ID || type == td_api::destroy::ID || type == td_api::logOut::ID) {
      push({client, 0,
            td_api::make_object<td_api::updateAuthorizationState>(
                td_api::make_object<td_api::authorizationStateClosed>())});
    }
  }
  void emit_state(std::int32_t client, td_api::object_ptr<td_api::AuthorizationState> state) {
    push({client, 0, td_api::make_object<td_api::updateAuthorizationState>(std::move(state))});
  }
  void emit_update(std::int32_t client, td_api::object_ptr<td_api::Object> update) {
    push({client, 0, std::move(update)});
  }
  std::vector<std::pair<std::int32_t, std::int32_t>> sent() {
    std::lock_guard lock(mutex_);
    return sent_;
  }
  bool code_matches(const std::string &expected) {
    std::lock_guard lock(mutex_);
    return last_code_ == expected;
  }
  void queue_response(std::int32_t function, td_api::object_ptr<td_api::Object> response) {
    std::lock_guard lock(mutex_);
    scripted_[function].push_back(std::move(response));
  }
  std::size_t pending_responses() {
    std::lock_guard lock(mutex_);
    std::size_t total = 0;
    for (const auto &[id, queue] : scripted_) {
      static_cast<void>(id);
      total += queue.size();
    }
    return total;
  }
  void fail_next_send() { fail_send_.store(true); }
  void set_fail_add_proxy(bool value) { fail_add_proxy_.store(value); }
  void set_suppress_close_updates(bool value) { suppress_close_updates_.store(value); }
  telebezel::TransportResponse receive(double timeout) override {
    std::unique_lock lock(mutex_);
    condition_.wait_for(lock, std::chrono::duration<double>(timeout), [this] { return !responses_.empty(); });
    if (responses_.empty())
      return {};
    auto response = std::move(responses_.front());
    responses_.pop();
    return response;
  }

private:
  void push(telebezel::TransportResponse response) {
    std::lock_guard lock(mutex_);
    responses_.push(std::move(response));
    condition_.notify_one();
  }
  std::atomic<std::int32_t> next_client_{1};
  std::string last_code_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<telebezel::TransportResponse> responses_;
  std::vector<std::pair<std::int32_t, std::int32_t>> sent_;
  std::map<std::int32_t, std::deque<td_api::object_ptr<td_api::Object>>> scripted_;
  std::atomic<bool> fail_send_{false};
  std::atomic<bool> fail_add_proxy_{false};
  std::atomic<bool> suppress_close_updates_{false};
};

} // namespace telebezel::testing
