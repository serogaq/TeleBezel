#pragma once
#include <cstdint>
#include <td/telegram/Client.h>
#include <td/telegram/td_api.hpp>

namespace telebezel {
struct TransportResponse {
  std::int32_t client_id{0};
  std::uint64_t request_id{0};
  td::td_api::object_ptr<td::td_api::Object> object;
};
class TdTransport {
public:
  virtual ~TdTransport() = default;
  virtual std::int32_t create_client_id() = 0;
  virtual void send(std::int32_t client_id, std::uint64_t request_id,
                    td::td_api::object_ptr<td::td_api::Function> function) = 0;
  virtual TransportResponse receive(double timeout) = 0;
};
class NativeTdTransport final : public TdTransport {
public:
  std::int32_t create_client_id() override;
  void send(std::int32_t client_id, std::uint64_t request_id,
            td::td_api::object_ptr<td::td_api::Function> function) override;
  TransportResponse receive(double timeout) override;

private:
  td::ClientManager manager_;
};
} // namespace telebezel
