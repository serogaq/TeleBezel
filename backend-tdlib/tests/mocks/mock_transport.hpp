#pragma once
#include "telebezel/transport.hpp"
#include <gmock/gmock.h>

namespace telebezel::testing {
class MockTransport : public TdTransport {
public:
  MOCK_METHOD(std::string, initialize, (), (override));
  MOCK_METHOD(std::int32_t, create_client_id, (), (override));
  MOCK_METHOD(void, send, (std::int32_t, std::uint64_t, td::td_api::object_ptr<td::td_api::Function>), (override));
  MOCK_METHOD(TransportResponse, receive, (double), (override));
};
} // namespace telebezel::testing
