#include "telebezel/config.hpp"
#include "telebezel/crypto.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/status.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("test assertion failed");
  }
}
} // namespace

int main() {
  using telebezel::ProxyMode;
  require(telebezel::parse_proxy_mode("direct") == ProxyMode::direct);
  require(telebezel::parse_proxy_mode("socks5") == ProxyMode::socks5);
  require(telebezel::parse_proxy_mode("http") == ProxyMode::http);
  require(telebezel::parse_proxy_mode("mtproto") == ProxyMode::mtproto);
  bool rejected = false;
  try {
    static_cast<void>(telebezel::parse_proxy_mode("invalid"));
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  require(telebezel::token_matches("secret-a", "secret-a"));
  require(!telebezel::token_matches("secret-a", "secret-b"));
  std::array<std::uint8_t, 32> master_key{};
  for (std::size_t index = 0; index < master_key.size(); ++index) {
    master_key[index] = static_cast<std::uint8_t>(index);
  }
  const auto derived = telebezel::derive_database_key(master_key, "00112233-4455-4677-8899-aabbccddeeff");
  require(telebezel::hex_encode(derived.data(), derived.size()) ==
          "dffbc48560404bbdf166f3015cfd8fa61ac4cf9fdab4256af783bdda22046181");
  require(telebezel::valid_uuid("00112233-4455-4677-8899-aabbccddeeff"));
  require(!telebezel::valid_uuid("../../escape"));
  telebezel::StatusSnapshot status{true, "1.8.67", 0};
  const std::string json = telebezel::status_json(status, "request-id");
  require(json.find("\"status\":\"ready\"") != std::string::npos);
  require(json.find("\"accounts\":0") != std::string::npos);
  for (const auto &fixture : {"tdlib_status_ready.json", "tdlib_status_not_ready.json"}) {
    std::ifstream stream(std::string(TELEBEZEL_CONTRACT_FIXTURE_DIR) + "/" + fixture);
    require(static_cast<bool>(stream));
    nlohmann::json expected;
    stream >> expected;
    const bool is_ready = std::string(fixture) == "tdlib_status_ready.json";
    telebezel::StatusSnapshot snapshot{is_ready, is_ready ? "1.8.67" : "", 0};
    require(nlohmann::json::parse(telebezel::status_json(snapshot, "fixture-request-id")) == expected);
  }
  setenv("TDLIB_INTERNAL_TOKEN", "0123456789abcdef0123456789abcdef", 1);
  setenv("TDLIB_LISTEN_PORT", "8081junk", 1);
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  unsetenv("TDLIB_LISTEN_PORT");
  setenv("TDLIB_INTERNAL_TOKEN_FILE", "/nonexistent-telebezel-secret", 1);
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  unsetenv("TDLIB_INTERNAL_TOKEN_FILE");
  setenv("TDLIB_PROXY_MODE", "socks5", 1);
  setenv("TDLIB_PROXY_HOST", "proxy.example", 1);
  setenv("TDLIB_PROXY_PORT", "1080", 1);
  require(telebezel::load_config().proxy.port == 1080);
  setenv("TDLIB_PROXY_MODE", "http", 1);
  setenv("TDLIB_PROXY_HTTP_ONLY", "true", 1);
  require(telebezel::load_config().proxy.http_only);
  unsetenv("TDLIB_PROXY_HTTP_ONLY");
  setenv("TDLIB_PROXY_MODE", "mtproto", 1);
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  setenv("TDLIB_PROXY_SECRET", "not-logged-secret", 1);
  require(telebezel::load_config().proxy.mode == ProxyMode::mtproto);
  unsetenv("TDLIB_PROXY_SECRET");
  unsetenv("TDLIB_PROXY_MODE");
  rejected = false;
  try {
    static_cast<void>(telebezel::load_config());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  const auto registry_root =
      std::filesystem::temp_directory_path() / ("telebezel-registry-" + telebezel::make_request_id());
  std::filesystem::create_directories(registry_root);
  const auto master_path = registry_root / "master-key";
  {
    std::ofstream output(master_path);
    output << "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n";
  }
  std::filesystem::permissions(master_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                   std::filesystem::perms::group_read,
                               std::filesystem::perm_options::replace);
  require(telebezel::read_master_key(master_path)[0] == 0);
  std::filesystem::permissions(master_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                   std::filesystem::perms::others_read,
                               std::filesystem::perm_options::replace);
  rejected = false;
  try {
    static_cast<void>(telebezel::read_master_key(master_path));
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected);
  std::filesystem::permissions(master_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
  require(telebezel::read_master_key(master_path)[0] == 0);
  {
    telebezel::Registry registry(registry_root, master_path);
    registry.open();
    const std::string uuid = "00112233-4455-4677-8899-aabbccddeeff";
    const std::string generation = "11112233-4455-4677-8899-aabbccddeeff";
    const std::string orphan = "20112233-4455-4677-8899-aabbccddeeff";
    const std::string corrupt = "30112233-4455-4677-8899-aabbccddeeff";
    const std::string unsafe = "50112233-4455-4677-8899-aabbccddeeff";
    registry.ensure_account_directories(uuid);
    require(registry.account_directory_exists(uuid));
    registry.write({1, uuid, generation, false, 1, 7, "active", "", false, nullptr, "", ""},
                   nlohmann::json{{"mode", "direct"}, {"http_only", false}});
    const auto manifest = registry.read(uuid);
    require(manifest.has_value());
    require(manifest->generation == generation);
    require(manifest->revision == 7);
    {
      std::ifstream stored(registry_root / "registry" / (uuid + ".json"));
      const std::string encoded((std::istreambuf_iterator<char>(stored)), std::istreambuf_iterator<char>());
      require(encoded.find("proxy_protected") != std::string::npos);
      require(encoded.find("\"proxy\"") == std::string::npos);
      require(encoded.find("direct") == std::string::npos);
    }
    registry.ensure_account_directories(orphan);
    require(registry.orphan_account_ids() == std::vector<std::string>{orphan});
    {
      std::ofstream output(registry_root / "registry" / (corrupt + ".json"));
      output << "not-json";
    }
    std::filesystem::permissions(registry_root / "registry" / (corrupt + ".json"),
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
    require(registry.discover().size() == 1);
    require(registry.manifest_errors().at(corrupt) == "storage.corrupt");
    registry.ensure_account_directories(unsafe);
    std::filesystem::create_symlink(registry_root / "registry" / (corrupt + ".json"),
                                    registry_root / "accounts" / unsafe / "db" / "unsafe-link");
    rejected = false;
    try {
      registry.remove_account_directory(unsafe);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()) == "storage.unsafe_path";
    }
    require(rejected);
    bool second_owner_rejected = false;
    try {
      telebezel::Registry second_owner(registry_root, master_path);
      second_owner.open();
    } catch (const std::runtime_error &) {
      second_owner_rejected = true;
    }
    require(second_owner_rejected);
    registry.remove_account_directory(uuid);
    require(!registry.account_directory_exists(uuid));
  }
  std::filesystem::remove_all(registry_root);
  return 0;
}
