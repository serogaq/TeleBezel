#pragma once
#include "telebezel/status.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <td/telegram/Client.h>
#include <td/telegram/td_api.hpp>
#include <thread>

namespace telebezel {
class TdRuntime final {
public:
  TdRuntime();
  ~TdRuntime();
  TdRuntime(const TdRuntime &) = delete;
  TdRuntime &operator=(const TdRuntime &) = delete;
  void start();
  void stop();
  StatusSnapshot status() const;

private:
  void receive_loop();
  std::unique_ptr<td::ClientManager> manager_;
  std::thread receive_thread_;
  std::atomic<bool> stopping_{false};
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  StatusSnapshot status_;
  std::int32_t probe_client_id_{0};
  std::uint64_t probe_request_id_{1};
};
} // namespace telebezel
