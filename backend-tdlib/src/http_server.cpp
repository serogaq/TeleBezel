#include "telebezel/http_server.hpp"
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string_view>

namespace telebezel {
namespace {
std::mutex log_mutex;
void json_response(httplib::Response &response, int status, const std::string &body,
                   const std::string &request_id = {}) {
  response.status = status;
  response.set_content(body, "application/json");
  response.set_header("Cache-Control", "no-store");
  if (!request_id.empty()) {
    response.set_header("X-Request-ID", request_id);
  }
}
std::string bearer_token(const httplib::Request &request) {
  const std::string value = request.get_header_value("Authorization");
  constexpr std::string_view prefix{"Bearer "};
  return value.starts_with(prefix) ? value.substr(prefix.size()) : std::string{};
}
} // namespace

HttpServer::HttpServer(const Config &config, TdRuntime &runtime) : config_(config), runtime_(runtime) {
  server_.Get("/healthz", [](const httplib::Request &, httplib::Response &response) {
    json_response(response, 200, health_json(), make_request_id());
  });
  server_.Get("/internal/v1/status", [this](const httplib::Request &request, httplib::Response &response) {
    const std::string request_id = make_request_id();
    if (!token_matches(config_.internal_token, bearer_token(request))) {
      json_response(response, 401,
                    nlohmann::json{{"error", {{"code", "auth.unauthorized"}}}, {"request_id", request_id}}.dump(),
                    request_id);
      return;
    }
    const StatusSnapshot snapshot = runtime_.status();
    json_response(response, snapshot.ready ? 200 : 503, status_json(snapshot, request_id), request_id);
  });
  server_.set_error_handler([](const httplib::Request &, httplib::Response &response) {
    if (response.body.empty()) {
      json_response(response, response.status, nlohmann::json{{"error", {{"code", "http.not_found"}}}}.dump(),
                    make_request_id());
    }
  });
  server_.set_logger([](const httplib::Request &request, const httplib::Response &response) {
    const std::string route =
        request.path == "/healthz" || request.path == "/internal/v1/status" ? request.path : "unmatched";
    const nlohmann::json event{{"event", "http_request"},
                               {"request_id", response.get_header_value("X-Request-ID")},
                               {"method", request.method},
                               {"route", route},
                               {"status", response.status}};
    std::lock_guard<std::mutex> guard(log_mutex);
    std::cerr << event.dump() << '\n';
  });
}

bool HttpServer::listen() { return server_.listen(config_.listen_address, config_.listen_port); }
void HttpServer::stop() { server_.stop(); }
} // namespace telebezel
