#pragma once
#include "telebezel/transport.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
namespace telebezel::runtime {
struct Pending {
  std::int32_t client_id;
  std::promise<td::td_api::object_ptr<td::td_api::Object>> promise;
};
struct Unresolved {
  std::int32_t client_id;
  std::chrono::steady_clock::time_point recovery_at;
  bool recover_client;
};
struct RequestHandle {
  std::uint64_t request_id;
  std::int32_t client_id;
  std::future<td::td_api::object_ptr<td::td_api::Object>> future;
  bool pending;
};
struct AsyncRequest {
  std::int32_t client_id;
  std::chrono::steady_clock::time_point deadline;
};

struct RequestCounts {
  std::size_t pending;
  std::size_t unresolved;
  std::size_t asynchronous;
};
class TdRequestBroker final {
public:
  explicit TdRequestBroker(TdTransport &transport) : transport_(transport) {}
  std::uint64_t next_id();
  td::td_api::object_ptr<td::td_api::Object> request(std::int32_t client_id,
                                                     td::td_api::object_ptr<td::td_api::Function> function,
                                                     std::chrono::seconds timeout = std::chrono::seconds(8),
                                                     bool recover_stalled_client = false);
  RequestHandle begin_request(std::int32_t client_id, td::td_api::object_ptr<td::td_api::Function> function);
  td::td_api::object_ptr<td::td_api::Object> await_request(RequestHandle handle, std::chrono::seconds timeout,
                                                           bool recover_stalled_client = false);
  std::optional<std::string> request_identity(std::int32_t client_id);
  // Returns true only for a correlated identity response; the caller projects it.
  bool complete(TransportResponse &response);
  std::vector<std::int32_t> expire(std::chrono::steady_clock::time_point now);
  void start();
  void stop();
  RequestCounts counts() const;

private:
  TdTransport &transport_;
  mutable std::mutex mutex_;
  bool stopping_{false};
  std::atomic<std::uint64_t> next_request_id_{1};
  std::unordered_map<std::uint64_t, Pending> pending_;
  std::unordered_map<std::uint64_t, Unresolved> unresolved_;
  std::unordered_map<std::uint64_t, AsyncRequest> async_requests_;
};
} // namespace telebezel::runtime
