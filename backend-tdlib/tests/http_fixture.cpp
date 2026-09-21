// Test-only executable. Production HTTP/runtime/registry, scripted Telegram transport.
// Control is stdin/stdout, never a route or environment switch in the production binary.
#include "fakes/fake_transport.hpp"
#include "telebezel/http_server.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
namespace api = td::td_api;
using Json = nlohmann::json;
const std::map<std::string, std::int32_t> functions{{"phone", api::setAuthenticationPhoneNumber::ID},
                                                    {"code", api::checkAuthenticationCode::ID},
                                                    {"password", api::checkAuthenticationPassword::ID},
                                                    {"email", api::setAuthenticationEmailAddress::ID},
                                                    {"email_code", api::checkAuthenticationEmailCode::ID},
                                                    {"qr", api::requestQrCodeAuthentication::ID},
                                                    {"resend", api::resendAuthenticationCode::ID},
                                                    {"resend_email", api::resendLoginEmailAddressCode::ID},
                                                    {"chats", api::loadChats::ID},
                                                    {"history", api::getChatHistory::ID},
                                                    {"message", api::getMessageLocally::ID},
                                                    {"open", api::openChat::ID},
                                                    {"close", api::closeChat::ID},
                                                    {"logout", api::logOut::ID},
                                                    {"destroy", api::destroy::ID},
                                                    {"add_proxy", api::addProxy::ID},
                                                    {"ping", api::pingProxy::ID}};
api::object_ptr<api::message> message(const Json &data) {
  auto result = api::make_object<api::message>();
  result->id_ = std::stoll(data.at("id").get<std::string>());
  result->chat_id_ = std::stoll(data.value("chat", "42"));
  result->sender_id_ = api::make_object<api::messageSenderUser>(9007199254740993LL);
  result->date_ = 1700000000;
  if (!data.value("unsupported", false)) {
    auto content = api::make_object<api::messageText>();
    content->text_ = api::make_object<api::formattedText>();
    content->text_->text_ = data.value("text", "Hello Telegram");
    result->content_ = std::move(content);
  }
  return result;
}
api::object_ptr<api::AuthorizationState> state(const std::string &name) {
  if (name == "ready")
    return api::make_object<api::authorizationStateReady>();
  if (name == "awaiting_phone_number")
    return api::make_object<api::authorizationStateWaitPhoneNumber>();
  if (name == "awaiting_password")
    return api::make_object<api::authorizationStateWaitPassword>();
  if (name == "awaiting_email_address")
    return api::make_object<api::authorizationStateWaitEmailAddress>();
  if (name == "awaiting_email_code")
    return api::make_object<api::authorizationStateWaitEmailCode>();
  if (name == "registration_required")
    return api::make_object<api::authorizationStateWaitRegistration>();
  if (name == "premium_purchase_required")
    return api::make_object<api::authorizationStateWaitPremiumPurchase>();
  if (name == "awaiting_qr_confirmation")
    return api::make_object<api::authorizationStateWaitOtherDeviceConfirmation>("tg://login?token=fake");
  if (name == "awaiting_code")
    return api::make_object<api::authorizationStateWaitCode>(api::make_object<api::authenticationCodeInfo>(
        "+15550000000", api::make_object<api::authenticationCodeTypeSms>(6),
        api::make_object<api::authenticationCodeTypeCall>(6), 0));
  throw std::runtime_error("Unknown fixture state");
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  telebezel::Config config;
  config.listen_address = "127.0.0.1";
  config.listen_port = static_cast<std::uint16_t>(std::stoi(argv[1]));
  config.internal_token = "integration-internal-token";
  config.telegram_api_id = 12345;
  config.telegram_api_hash = std::string(32, 'a');
  config.data_directory = argv[2];
  config.master_key_file = config.data_directory + "/master-key";
  std::filesystem::create_directories(config.data_directory);
  {
    std::ofstream key(config.master_key_file);
    key << std::string(64, 'a');
  }
  std::filesystem::permissions(config.master_key_file,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
  telebezel::Registry registry(config.data_directory, config.master_key_file);
  registry.open();
  auto owned_transport = std::make_unique<telebezel::testing::FakeTransport>();
  auto *transport = owned_transport.get();
  telebezel::TdRuntime runtime(config, registry, std::move(owned_transport));
  runtime.start();
  telebezel::HttpServer server(config, runtime);
  const int port = server.bind();
  if (port <= 0)
    throw std::runtime_error("Fixture HTTP bind failed");
  std::thread http([&] { server.listen_bound(); });
  std::cout << Json{{"port", port}}.dump() << std::endl;
  std::string line;
  while (std::getline(std::cin, line)) {
    try {
      const auto command = Json::parse(line);
      const auto op = command.at("op").get<std::string>();
      const auto client = command.value("client", 1);
      if (op == "state") {
        transport->emit_state(client, state(command.at("state")));
      } else if (op == "response") {
        const auto function = functions.at(command.at("function"));
        const auto kind = command.value("kind", "error");
        api::object_ptr<api::Object> response;
        if (kind == "error")
          response = api::make_object<api::error>(command.value("code", 400),
                                                  command.value("message", "SECRET_TELEGRAM_ERROR"));
        else if (kind == "history") {
          auto history = api::make_object<api::messages>();
          for (const auto &item : command.at("items"))
            history->messages_.push_back(message(item));
          history->total_count_ = static_cast<std::int32_t>(history->messages_.size());
          response = std::move(history);
        } else if (kind == "message")
          response = message(command.at("item"));
        else if (kind == "ok")
          response = api::make_object<api::ok>();
        else if (kind != "timeout")
          throw std::runtime_error("Unknown response");
        transport->queue_response(function, std::move(response));
      } else if (op == "chat") {
        auto chat = api::make_object<api::chat>();
        chat->id_ = std::stoll(command.value("id", "42"));
        chat->title_ = command.value("title", "Integration chat");
        chat->type_ = api::make_object<api::chatTypePrivate>(9007199254740993LL);
        api::object_ptr<api::ChatList> list =
            command.value("list", "main") == "main"
                ? api::object_ptr<api::ChatList>(api::make_object<api::chatListMain>())
                : api::object_ptr<api::ChatList>(api::make_object<api::chatListArchive>());
        chat->positions_.push_back(
            api::make_object<api::chatPosition>(std::move(list), command.value("order", 100LL), false, nullptr));
        transport->emit_update(client, api::make_object<api::updateNewChat>(std::move(chat)));
      } else if (op == "title") {
        transport->emit_update(client, api::make_object<api::updateChatTitle>(std::stoll(command.value("chat", "42")),
                                                                              command.at("title")));
      } else if (op == "new_message") {
        transport->emit_update(client, api::make_object<api::updateNewMessage>(message(command.at("item"))));
      } else if (op == "delete_message") {
        transport->emit_update(client, api::make_object<api::updateDeleteMessages>(
                                           std::stoll(command.value("chat", "42")),
                                           std::vector<std::int64_t>{std::stoll(command.at("id").get<std::string>())},
                                           true, false));
      } else if (op != "stats")
        throw std::runtime_error("Unknown fixture operation");
      Json counts = Json::object();
      for (const auto &[name, id] : functions) {
        counts[name] = 0;
        for (const auto &[sent_client, sent_id] : transport->sent())
          if (sent_client == client && sent_id == id)
            counts[name] = counts[name].get<int>() + 1;
      }
      std::cout << Json{{"counts", counts},
                        {"pending", transport->pending_responses()},
                        {"code_matches", transport->code_matches(command.value("expected_code", ""))}}
                       .dump()
                << std::endl;
    } catch (const std::exception &error) {
      std::cout << Json{{"error", error.what()}}.dump() << std::endl;
    }
  }
  server.stop();
  http.join();
  runtime.stop();
}
