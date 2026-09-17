#include "telebezel/td_runtime.hpp"
#include <chrono>
#include <td/telegram/td_api.h>

namespace telebezel {
namespace td_api = td::td_api;

TdRuntime::TdRuntime() = default;
TdRuntime::~TdRuntime() { stop(); }

void TdRuntime::start() {
  if (manager_) {
    return;
  }
  td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
  manager_ = std::make_unique<td::ClientManager>();
  probe_client_id_ = manager_->create_client_id();
  stopping_.store(false);
  receive_thread_ = std::thread(&TdRuntime::receive_loop, this);
  manager_->send(probe_client_id_, probe_request_id_, td_api::make_object<td_api::getOption>("version"));
  std::unique_lock lock(mutex_);
  condition_.wait_for(lock, std::chrono::seconds(5), [this] { return status_.ready || stopping_.load(); });
}

void TdRuntime::stop() {
  stopping_.store(true);
  condition_.notify_all();
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }
  manager_.reset();
}

StatusSnapshot TdRuntime::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

void TdRuntime::receive_loop() {
  while (!stopping_.load()) {
    auto response = manager_->receive(0.1);
    if (!response.object || response.client_id != probe_client_id_ || response.request_id != probe_request_id_) {
      continue;
    }
    if (response.object->get_id() == td_api::optionValueString::ID) {
      auto value = td::move_tl_object_as<td_api::optionValueString>(response.object);
      {
        std::lock_guard lock(mutex_);
        status_.ready = true;
        status_.tdlib_version = value->value_;
        status_.account_count = 0;
      }
      condition_.notify_all();
    }
  }
}
} // namespace telebezel
