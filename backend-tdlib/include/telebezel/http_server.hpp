#pragma once
#include "telebezel/config.hpp"
#include "telebezel/td_runtime.hpp"
#include <httplib.h>

namespace telebezel {
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
  httplib::Server server_;
};
} // namespace telebezel
