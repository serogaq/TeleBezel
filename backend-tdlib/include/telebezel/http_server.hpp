#pragma once
#include "telebezel/config.hpp"
#include "telebezel/td_runtime.hpp"
#include <httplib.h>
#include <map>
#include <mutex>
#include <string>

namespace telebezel {
// Bounds how many read requests one account may hold at once. The HTTP pool is
// shared, so an account whose TDLib client has stalled must not be able to take
// every worker and stall reads for every other account.
class AccountReadLimiter final {
public:
  explicit AccountReadLimiter(std::size_t limit) : limit_(limit) {}
  bool acquire(const std::string &uuid);
  void release(const std::string &uuid);

private:
  std::mutex mutex_;
  std::map<std::string, std::size_t> in_flight_;
  std::size_t limit_;
};
class HttpServer final {
public:
  HttpServer(const Config &config, TdRuntime &runtime);
  int bind();
  bool listen_bound();
  bool listen();
  void stop();

private:
  const Config &config_;
  TdRuntime &runtime_;
  AccountReadLimiter reads_{2};
  httplib::Server server_;
};
} // namespace telebezel
