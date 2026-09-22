#pragma once
#include "telebezel/config.hpp"
#include "telebezel/runtime/support.hpp"
#include <string>
#include <vector>
namespace telebezel::runtime::reads {
inline constexpr const char *preview_purpose = "telebezel/preview/v1";
inline constexpr const char *chats_cursor_purpose = "telebezel/cursor/chats/v1";
inline constexpr const char *history_cursor_purpose = "telebezel/cursor/history/v1";
std::string preview_token(const Config &config, const std::string &uuid, const ReadFence &fence, std::int64_t chat_id,
                          std::int64_t message_id, std::int32_t file_id);
std::string preview_key(const std::string &uuid, const ReadFence &fence, std::int32_t file_id);
std::string cursor_signature(const Config &config, const char *purpose, const std::string &uuid,
                             const std::string &generation, const std::string &payload);
std::vector<std::string> split_fields(const std::string &payload, char separator);
std::string base64_encode(const std::string &bytes);
std::int64_t projection_id(const nlohmann::json &item);
const nlohmann::json *changed_since(const AccountState &account, const ReadFence &fence, std::int64_t chat_id,
                                    std::int64_t message_id);
bool previewable(const Config &config, std::int32_t file_id, std::int64_t size, const std::string &mime);
} // namespace telebezel::runtime::reads
