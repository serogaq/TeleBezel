#include "telebezel/runtime/request_broker.hpp"
#include <algorithm>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
namespace td_api = td::td_api;
std::uint64_t TdRequestBroker::next_id() {
  auto value = next_request_id_.fetch_add(1);
  if (value == 0)
    value = next_request_id_.fetch_add(1);
  return value;
}

td_api::object_ptr<td_api::Object> TdRequestBroker::request(std::int32_t client_id,
                                                            td_api::object_ptr<td_api::Function> function,
                                                            std::chrono::milliseconds timeout,
                                                            bool recover_stalled_client) {
  return await_request(begin_request(client_id, std::move(function)), timeout, recover_stalled_client);
}
RequestHandle TdRequestBroker::begin_request(std::int32_t client_id, td_api::object_ptr<td_api::Function> function) {
  std::lock_guard lock(mutex_);
  std::promise<td_api::object_ptr<td_api::Object>> promise;
  auto future = promise.get_future();
  if (stopping_ || pending_.size() + async_requests_.size() >= 1024) {
    promise.set_value(td_api::make_object<td_api::error>(503, stopping_ ? "service.stopping" : "service.busy"));
    return {0, client_id, std::move(future), false};
  }
  const auto id = next_id();
  pending_.emplace(id, Pending{client_id, std::move(promise)});
  try {
    // send only enqueues work. Serializing it with stop prevents sends after shutdown.
    transport_.send(client_id, id, std::move(function));
  } catch (...) {
    pending_.erase(id);
    throw;
  }
  return {id, client_id, std::move(future), true};
}
td_api::object_ptr<td_api::Object>
TdRequestBroker::await_request(RequestHandle handle, std::chrono::milliseconds timeout, bool recover_stalled_client) {
  if (handle.future.wait_for(timeout) != std::future_status::ready) {
    std::lock_guard lock(mutex_);
    // The response may have completed between wait_for and acquiring the lock.
    if (handle.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
      return handle.future.get();
    pending_.erase(handle.request_id);
    if (handle.pending && unresolved_.size() >= 1024)
      evict_unresolved();
    if (handle.pending)
      unresolved_[handle.request_id] = {handle.client_id, std::chrono::steady_clock::now() + std::chrono::seconds(22),
                                        recover_stalled_client};
    return td_api::make_object<td_api::error>(504, "operation.outcome_unknown");
  }
  return handle.future.get();
}
void TdRequestBroker::evict_unresolved() {
  auto victim = unresolved_.end();
  for (auto item = unresolved_.begin(); item != unresolved_.end(); ++item) {
    if (victim == unresolved_.end() || (victim->second.recover_client && !item->second.recover_client) ||
        (victim->second.recover_client == item->second.recover_client &&
         item->second.recovery_at < victim->second.recovery_at))
      victim = item;
  }
  if (victim != unresolved_.end())
    unresolved_.erase(victim);
}
std::optional<std::string> TdRequestBroker::request_identity(std::int32_t client_id) {
  std::lock_guard lock(mutex_);
  if (client_id == 0 || stopping_)
    return "service.stopping";
  if (pending_.size() + async_requests_.size() >= 1024)
    return "service.busy";
  const auto id = next_id();
  async_requests_.emplace(id, AsyncRequest{client_id, std::chrono::steady_clock::now() + std::chrono::seconds(8)});
  try {
    transport_.send(client_id, id, td_api::make_object<td_api::getMe>());
  } catch (const std::exception &) {
    async_requests_.erase(id);
    return "service.tdlib_unavailable";
  }
  return std::nullopt;
}
bool TdRequestBroker::complete(TransportResponse &response) {
  std::lock_guard lock(mutex_);
  if (const auto item = async_requests_.find(response.request_id); item != async_requests_.end()) {
    if (item->second.client_id != response.client_id)
      return false;
    async_requests_.erase(item);
    return true;
  }
  if (const auto item = pending_.find(response.request_id); item != pending_.end()) {
    if (item->second.client_id == response.client_id) {
      item->second.promise.set_value(std::move(response.object));
      pending_.erase(item);
    }
  } else if (const auto late = unresolved_.find(response.request_id);
             late != unresolved_.end() && late->second.client_id == response.client_id) {
    unresolved_.erase(late);
  }
  return false;
}
std::vector<std::int32_t> TdRequestBroker::expire(std::chrono::steady_clock::time_point now) {
  std::lock_guard lock(mutex_);
  std::vector<std::int32_t> stalled;
  for (auto item = unresolved_.begin(); item != unresolved_.end();) {
    if (item->second.recovery_at > now) {
      ++item;
      continue;
    }
    if (item->second.recover_client &&
        std::find(stalled.begin(), stalled.end(), item->second.client_id) == stalled.end())
      stalled.push_back(item->second.client_id);
    item = unresolved_.erase(item);
  }
  for (auto item = async_requests_.begin(); item != async_requests_.end();) {
    if (item->second.deadline <= now)
      item = async_requests_.erase(item);
    else
      ++item;
  }
  return stalled;
}
void TdRequestBroker::start() {
  std::lock_guard lock(mutex_);
  stopping_ = false;
}
void TdRequestBroker::stop() {
  std::lock_guard lock(mutex_);
  stopping_ = true;
  for (auto &[id, item] : pending_) {
    static_cast<void>(id);
    item.promise.set_value(td_api::make_object<td_api::error>(503, "service.stopping"));
  }
  pending_.clear();
  async_requests_.clear();
  unresolved_.clear();
}
RequestCounts TdRequestBroker::counts() const {
  std::lock_guard lock(mutex_);
  return {pending_.size(), unresolved_.size(), async_requests_.size()};
}
} // namespace telebezel::runtime
