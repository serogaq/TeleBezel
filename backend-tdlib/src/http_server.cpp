#include "telebezel/http_server.hpp"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
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
std::string request_id(const httplib::Request &request) {
  const std::string provided = request.get_header_value("X-Request-ID");
  static const std::regex uuid(
      "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$");
  return std::regex_match(provided, uuid) ? provided : make_request_id();
}
bool authorize(const Config &config, const httplib::Request &request, httplib::Response &response,
               const std::string &id) {
  if (token_matches(config.internal_token, bearer_token(request)))
    return true;
  json_response(response, 401, nlohmann::json{{"error", {{"code", "auth.unauthorized"}}}, {"request_id", id}}.dump(),
                id);
  return false;
}
nlohmann::json body(const httplib::Request &request, const std::set<std::string> &allowed,
                    const std::set<std::string> &required = {}) {
  if (request.body.size() > 16 * 1024)
    throw std::runtime_error("http.body_too_large");
  auto parsed = nlohmann::json::parse(request.body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object())
    throw std::runtime_error("request.invalid");
  for (const auto &[key, value] : parsed.items()) {
    static_cast<void>(value);
    if (!allowed.contains(key))
      throw std::runtime_error("request.invalid");
  }
  for (const auto &key : required) {
    if (!parsed.contains(key))
      throw std::runtime_error("request.invalid");
  }
  if (parsed.contains("proxy") && !parsed["proxy"].is_object())
    throw std::runtime_error("request.invalid");
  if (parsed.contains("proxy")) {
    static const std::set<std::string> proxy_fields{"id",        "mode",     "host",     "port",
                                                    "http_only", "username", "password", "secret"};
    for (const auto &[key, value] : parsed["proxy"].items()) {
      static_cast<void>(value);
      if (!proxy_fields.contains(key))
        throw std::runtime_error("request.invalid");
    }
  }
  return parsed;
}
std::int64_t integer_parameter(const httplib::Request &request, const char *name) {
  const auto value = request.get_param_value(name);
  if (value.empty() || value.find_first_not_of("-0123456789") != std::string::npos)
    throw std::runtime_error("request.invalid");
  std::size_t parsed = 0;
  const auto result = std::stoll(value, &parsed);
  if (parsed != value.size())
    throw std::runtime_error("request.invalid");
  return result;
}
std::size_t limit_parameter(const httplib::Request &request, std::size_t fallback, std::size_t maximum) {
  if (!request.has_param("limit"))
    return fallback;
  const auto value = integer_parameter(request, "limit");
  if (value < 1 || static_cast<std::size_t>(value) > maximum)
    throw std::runtime_error("request.invalid");
  return static_cast<std::size_t>(value);
}
std::string lease_key(const httplib::Request &request, const std::string &view_id) {
  const auto type = request.get_param_value("principal_type");
  const auto id = request.get_param_value("principal_id");
  if ((type != "device" && type != "api_client") || id.empty() || !valid_uuid(view_id))
    throw std::runtime_error("request.invalid");
  return type + ":" + id + ":" + view_id;
}
nlohmann::json exception_result(const std::exception &exception) {
  static const std::set<std::string> known{"request.invalid",
                                           "http.body_too_large",
                                           "account.not_found",
                                           "account.gone",
                                           "operation.conflict",
                                           "operation.outcome_unknown",
                                           "storage.identity_mismatch",
                                           "storage.missing",
                                           "storage.corrupt",
                                           "storage.unsafe_path",
                                           "storage.invalid_key",
                                           "storage.io_error",
                                           "storage.volume_in_use",
                                           "configuration.missing",
                                           "configuration.invalid",
                                           "configuration.environment_mismatch",
                                           "chat.not_found",
                                           "message.not_found",
                                           "cursor.invalid",
                                           "cursor.unusable",
                                           "sync.resync_required",
                                           "interest.limit_reached",
                                           "service.busy",
                                           "service.stopping",
                                           "telegram.operation_failed"};
  const std::string detail = exception.what();
  const std::string code = known.contains(detail) ? detail : "service.internal";
  const int status = code == "request.invalid"                                                                ? 422
                     : code == "http.body_too_large"                                                          ? 413
                     : code == "account.not_found" || code == "chat.not_found" || code == "message.not_found" ? 404
                     : code == "account.gone"                                                                 ? 410
                     : code == "operation.outcome_unknown"                                                    ? 504
                     : code == "telegram.operation_failed"                                                    ? 502
                     : code == "interest.limit_reached"                                                       ? 429
                     : code == "service.internal"                                                             ? 500
                     : code == "storage.io_error" || code == "storage.volume_in_use"                          ? 503
                     : code.starts_with("service.")                                                           ? 503
                                                                                                              : 409;
  return {{"_error", true}, {"code", code}, {"status", status}};
}
void runtime_response(httplib::Response &response, const nlohmann::json &result, const std::string &id,
                      int success_status = 200) {
  if (result.value("_error", false)) {
    json_response(
        response, result.value("status", 500),
        nlohmann::json{{"error", {{"code", result.value("code", "service.internal")}}}, {"request_id", id}}.dump(), id);
  } else {
    json_response(response, success_status, nlohmann::json{{"data", result}, {"request_id", id}}.dump(), id);
  }
}
} // namespace

HttpServer::HttpServer(const Config &config, TdRuntime &runtime) : config_(config), runtime_(runtime) {
  server_.new_task_queue = [] { return new httplib::ThreadPool(4, 8, 64); };
  server_.set_payload_max_length(16 * 1024);
  server_.Get("/healthz", [](const httplib::Request &, httplib::Response &response) {
    json_response(response, 200, health_json(), make_request_id());
  });
  server_.Get("/internal/v1/status", [this](const httplib::Request &request, httplib::Response &response) {
    const std::string request_id = telebezel::request_id(request);
    if (!authorize(config_, request, response, request_id))
      return;
    const StatusSnapshot snapshot = runtime_.status();
    json_response(response, snapshot.ready ? 200 : 503, status_json(snapshot, request_id), request_id);
  });
  server_.Get("/internal/v1/accounts", [this](const httplib::Request &request, httplib::Response &response) {
    const std::string id = request_id(request);
    if (!authorize(config_, request, response, id))
      return;
    std::vector<std::string> ids;
    std::string value = request.get_param_value("ids");
    if (!value.empty()) {
      std::size_t position = 0;
      while (position <= value.size()) {
        const auto comma = value.find(',', position);
        ids.push_back(value.substr(position, comma == std::string::npos ? std::string::npos : comma - position));
        if (comma == std::string::npos)
          break;
        position = comma + 1;
      }
    }
    if (ids.size() > 50 ||
        std::any_of(ids.begin(), ids.end(), [](const std::string &account_id) { return !valid_uuid(account_id); })) {
      runtime_response(response, exception_result(std::runtime_error("request.invalid")), id);
      return;
    }
    runtime_response(response, runtime_.snapshots(ids), id);
  });
  server_.Get(R"(/internal/v1/accounts/([0-9a-f-]{36}))",
              [this](const httplib::Request &request, httplib::Response &response) {
                const std::string id = request_id(request);
                if (!authorize(config_, request, response, id))
                  return;
                runtime_response(response, runtime_.snapshot(request.matches[1]), id);
              });
  server_.Get(R"(/internal/v1/accounts/([0-9a-f-]{36})/chats)",
              [this](const httplib::Request &request, httplib::Response &response) {
                const std::string id = request_id(request);
                if (!authorize(config_, request, response, id))
                  return;
                try {
                  const auto list = request.get_param_value("list");
                  if (list != "main" && list != "archive")
                    throw std::runtime_error("request.invalid");
                  runtime_response(response,
                                   runtime_.chats(request.matches[1], list, limit_parameter(request, 20, 50),
                                                  request.get_param_value("cursor")),
                                   id);
                } catch (const std::exception &exception) {
                  runtime_response(response, exception_result(exception), id);
                }
              });
  server_.Get(R"(/internal/v1/accounts/([0-9a-f-]{36})/chats/(-?[0-9]+))", [this](const httplib::Request &request,
                                                                                  httplib::Response &response) {
    const std::string id = request_id(request);
    if (!authorize(config_, request, response, id))
      return;
    try {
      const auto chat_id = std::stoll(request.matches[2]);
      const auto key = lease_key(request, request.get_param_value("view_id")) + ":" + std::to_string(chat_id);
      auto interest = runtime_.set_interest(request.matches[1], chat_id, key, true);
      runtime_response(response,
                       interest.value("_error", false) ? interest : runtime_.chat(request.matches[1], chat_id), id);
    } catch (const std::exception &exception) {
      runtime_response(response, exception_result(exception), id);
    }
  });
  server_.Get(
      R"(/internal/v1/accounts/([0-9a-f-]{36})/chats/(-?[0-9]+)/messages)",
      [this](const httplib::Request &request, httplib::Response &response) {
        const std::string id = request_id(request);
        if (!authorize(config_, request, response, id))
          return;
        try {
          const auto chat_id = std::stoll(request.matches[2]);
          const auto key = lease_key(request, request.get_param_value("view_id")) + ":" + std::to_string(chat_id);
          auto interest = runtime_.set_interest(request.matches[1], chat_id, key, true);
          runtime_response(response,
                           interest.value("_error", false)
                               ? interest
                               : runtime_.messages(request.matches[1], chat_id, limit_parameter(request, 30, 50),
                                                   request.get_param_value("cursor")),
                           id);
        } catch (const std::exception &exception) {
          runtime_response(response, exception_result(exception), id);
        }
      });
  server_.Get(R"(/internal/v1/accounts/([0-9a-f-]{36})/chats/(-?[0-9]+)/messages/(-?[0-9]+))",
              [this](const httplib::Request &request, httplib::Response &response) {
                const std::string id = request_id(request);
                if (!authorize(config_, request, response, id))
                  return;
                try {
                  const auto chat_id = std::stoll(request.matches[2]);
                  const auto key =
                      lease_key(request, request.get_param_value("view_id")) + ":" + std::to_string(chat_id);
                  auto interest = runtime_.set_interest(request.matches[1], chat_id, key, true);
                  runtime_response(response,
                                   interest.value("_error", false)
                                       ? interest
                                       : runtime_.message(request.matches[1], chat_id, std::stoll(request.matches[3])),
                                   id);
                } catch (const std::exception &exception) {
                  runtime_response(response, exception_result(exception), id);
                }
              });
  server_.Get(R"(/internal/v1/accounts/([0-9a-f-]{36})/updates)",
              [this](const httplib::Request &request, httplib::Response &response) {
                const std::string id = request_id(request);
                if (!authorize(config_, request, response, id))
                  return;
                try {
                  runtime_response(response,
                                   runtime_.updates(request.matches[1], request.get_param_value("cursor"),
                                                    limit_parameter(request, 100, 100)),
                                   id);
                } catch (const std::exception &exception) {
                  runtime_response(response, exception_result(exception), id);
                }
              });
  const auto interest_handler = [this](const httplib::Request &request, httplib::Response &response, bool active) {
    const std::string id = request_id(request);
    if (!authorize(config_, request, response, id))
      return;
    try {
      const auto payload = body(request, {"principal_type", "principal_id"}, {"principal_type", "principal_id"});
      const auto view_id = request.matches[3].str();
      if (!valid_uuid(view_id))
        throw std::runtime_error("request.invalid");
      const auto key = payload.at("principal_type").get<std::string>() + ":" +
                       payload.at("principal_id").get<std::string>() + ":" + view_id + ":" + request.matches[2].str();
      runtime_response(response, runtime_.set_interest(request.matches[1], std::stoll(request.matches[2]), key, active),
                       id);
    } catch (const std::exception &exception) {
      runtime_response(response, exception_result(exception), id);
    }
  };
  server_.Put(R"(/internal/v1/accounts/([0-9a-f-]{36})/chats/(-?[0-9]+)/interests/([0-9a-f-]{36}))",
              [interest_handler](const httplib::Request &request, httplib::Response &response) {
                interest_handler(request, response, true);
              });
  server_.Delete(R"(/internal/v1/accounts/([0-9a-f-]{36})/chats/(-?[0-9]+)/interests/([0-9a-f-]{36}))",
                 [interest_handler](const httplib::Request &request, httplib::Response &response) {
                   interest_handler(request, response, false);
                 });
  server_.Delete(
      R"(/internal/v1/interests/principal)", [this](const httplib::Request &request, httplib::Response &response) {
        const std::string id = request_id(request);
        if (!authorize(config_, request, response, id))
          return;
        try {
          const auto payload = body(request, {"principal_type", "principal_id"}, {"principal_type", "principal_id"});
          const auto principal_type = payload.at("principal_type").get<std::string>();
          const auto principal_id = payload.at("principal_id").get<std::string>();
          if ((principal_type != "device" && principal_type != "api_client" && principal_type != "owner") ||
              !valid_uuid(principal_id))
            throw std::runtime_error("request.invalid");
          runtime_response(response, runtime_.release_interests(principal_type, principal_id), id);
        } catch (const std::exception &exception) {
          runtime_response(response, exception_result(exception), id);
        }
      });
  server_.Put(R"(/internal/v1/accounts/([0-9a-f-]{36}))", [this](const httplib::Request &request,
                                                                 httplib::Response &response) {
    const std::string id = request_id(request);
    if (!authorize(config_, request, response, id))
      return;
    try {
      runtime_response(response,
                       runtime_.reconcile(
                           request.matches[1],
                           body(request,
                                {"uuid", "generation", "authorization_generation", "revision", "effective_config_id",
                                 "telegram_api_id", "telegram_api_hash", "operation_id", "lifecycle", "proxy", "mode"},
                                {"uuid", "generation", "revision", "lifecycle", "proxy", "mode"})),
                       id);
    } catch (const std::exception &exception) {
      runtime_response(response, exception_result(exception), id);
    }
  });
  server_.Post(R"(/internal/v1/accounts/([0-9a-f-]{36})/authorization/actions)",
               [this](const httplib::Request &request, httplib::Response &response) {
                 const std::string id = request_id(request);
                 if (!authorize(config_, request, response, id))
                   return;
                 try {
                   runtime_response(response,
                                    runtime_.authorization_action(
                                        request.matches[1],
                                        body(request,
                                             {"uuid", "generation", "authorization_generation", "revision", "action",
                                              "authorization_version", "value"},
                                             {"uuid", "generation", "revision", "action", "authorization_version"})),
                                    id, 202);
                 } catch (const std::exception &exception) {
                   runtime_response(response, exception_result(exception), id);
                 }
               });
  server_.Post(R"(/internal/v1/accounts/([0-9a-f-]{36})/logout)",
               [this](const httplib::Request &request, httplib::Response &response) {
                 const std::string id = request_id(request);
                 if (!authorize(config_, request, response, id))
                   return;
                 try {
                   runtime_response(response,
                                    runtime_.logout(request.matches[1],
                                                    body(request,
                                                         {"uuid", "generation", "authorization_generation", "revision",
                                                          "effective_config_id", "telegram_api_id", "telegram_api_hash",
                                                          "operation_id", "logout_operation_id", "lifecycle", "proxy"},
                                                         {"uuid", "generation", "revision", "operation_id",
                                                          "logout_operation_id", "lifecycle", "proxy"})),
                                    id, 202);
                 } catch (const std::exception &exception) {
                   runtime_response(response, exception_result(exception), id);
                 }
               });
  server_.Put(R"(/internal/v1/accounts/([0-9a-f-]{36})/proxy)", [this](const httplib::Request &request,
                                                                       httplib::Response &response) {
    const std::string id = request_id(request);
    if (!authorize(config_, request, response, id))
      return;
    try {
      runtime_response(response,
                       runtime_.update_proxy(
                           request.matches[1],
                           body(request,
                                {"uuid", "generation", "authorization_generation", "revision", "effective_config_id",
                                 "telegram_api_id", "telegram_api_hash", "operation_id", "lifecycle", "proxy"},
                                {"uuid", "generation", "revision", "operation_id", "lifecycle", "proxy"})),
                       id, 202);
    } catch (const std::exception &exception) {
      runtime_response(response, exception_result(exception), id);
    }
  });
  server_.Post(R"(/internal/v1/accounts/([0-9a-f-]{36})/proxy/ping)",
               [this](const httplib::Request &request, httplib::Response &response) {
                 const std::string id = request_id(request);
                 if (!authorize(config_, request, response, id))
                   return;
                 try {
                   const auto payload = body(request, {"proxy"}, {"proxy"});
                   runtime_response(response, runtime_.ping_proxy(request.matches[1], payload.at("proxy")), id);
                 } catch (const std::exception &exception) {
                   runtime_response(response, exception_result(exception), id);
                 }
               });
  server_.Delete(
      R"(/internal/v1/accounts/([0-9a-f-]{36}))", [this](const httplib::Request &request, httplib::Response &response) {
        const std::string id = request_id(request);
        if (!authorize(config_, request, response, id))
          return;
        try {
          runtime_response(
              response,
              runtime_.remove(request.matches[1],
                              body(request,
                                   {"uuid", "generation", "authorization_generation", "revision", "effective_config_id",
                                    "telegram_api_id", "telegram_api_hash", "operation_id", "lifecycle", "proxy"},
                                   {"uuid", "generation", "revision", "operation_id", "lifecycle", "proxy"})),
              id, 202);
        } catch (const std::exception &exception) {
          runtime_response(response, exception_result(exception), id);
        }
      });
  server_.set_error_handler([](const httplib::Request &, httplib::Response &response) {
    if (response.body.empty()) {
      const std::string id = make_request_id();
      const std::string code = response.status == 413 ? "http.body_too_large" : "http.not_found";
      json_response(response, response.status, nlohmann::json{{"error", {{"code", code}}}, {"request_id", id}}.dump(),
                    id);
    }
  });
  server_.set_logger([](const httplib::Request &request, const httplib::Response &response) {
    const std::string route = request.path == "/healthz" || request.path == "/internal/v1/status" ? request.path
                              : request.path.starts_with("/internal/v1/accounts") ? "/internal/v1/accounts/{uuid}"
                                                                                  : "unmatched";
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
