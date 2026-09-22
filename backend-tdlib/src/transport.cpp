#include "telebezel/transport.hpp"
#include <stdexcept>

namespace telebezel {
std::string NativeTdTransport::initialize() {
  td::ClientManager::execute(td::td_api::make_object<td::td_api::setLogVerbosityLevel>(0));
  auto result = td::ClientManager::execute(td::td_api::make_object<td::td_api::getOption>("version"));
  if (!result || result->get_id() != td::td_api::optionValueString::ID)
    throw std::runtime_error("unable to read TDLib version");
  return static_cast<const td::td_api::optionValueString &>(*result).value_;
}

std::int32_t NativeTdTransport::create_client_id() { return manager_.create_client_id(); }
void NativeTdTransport::send(std::int32_t client_id, std::uint64_t request_id,
                             td::td_api::object_ptr<td::td_api::Function> function) {
  manager_.send(client_id, request_id, std::move(function));
}
TransportResponse NativeTdTransport::receive(double timeout) {
  auto response = manager_.receive(timeout);
  return {response.client_id, response.request_id, std::move(response.object)};
}
} // namespace telebezel
