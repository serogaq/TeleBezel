#include "telebezel/config.hpp"
#include "telebezel/crypto.hpp"
#include "telebezel/registry.hpp"
#include "telebezel/status.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {
class RegistryTest : public ::testing::Test {
protected:
  std::filesystem::path root, master;
  void SetUp() override {
    root = std::filesystem::temp_directory_path() / ("telebezel-registry-" + telebezel::make_request_id());
    std::filesystem::create_directories(root);
    master = root / "master-key";
    {
      std::ofstream output(master);
      output << "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n";
    }
    std::filesystem::permissions(master, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
  }
  void TearDown() override { std::filesystem::remove_all(root); }
};
class Environment {
public:
  void set(const std::string &key, const std::optional<std::string> &value) {
    if (!original_.contains(key)) {
      const auto *previous = std::getenv(key.c_str());
      original_[key] = previous ? std::optional<std::string>(previous) : std::nullopt;
    }
    value ? setenv(key.c_str(), value->c_str(), 1) : unsetenv(key.c_str());
  }
  ~Environment() {
    for (const auto &[key, value] : original_)
      value ? setenv(key.c_str(), value->c_str(), 1) : unsetenv(key.c_str());
  }

private:
  std::map<std::string, std::optional<std::string>> original_;
};
TEST(Config, ProxyModesAndInvalidValues) {
  using telebezel::ProxyMode;
  EXPECT_EQ(telebezel::parse_proxy_mode("direct"), ProxyMode::direct);
  EXPECT_EQ(telebezel::parse_proxy_mode("socks5"), ProxyMode::socks5);
  EXPECT_EQ(telebezel::parse_proxy_mode("http"), ProxyMode::http);
  EXPECT_EQ(telebezel::parse_proxy_mode("mtproto"), ProxyMode::mtproto);
  EXPECT_THROW(telebezel::parse_proxy_mode("invalid"), std::runtime_error);
}
TEST(Crypto, ConstantTimeTokensAndDerivedKeyVector) {
  EXPECT_TRUE(telebezel::token_matches("secret-a", "secret-a"));
  EXPECT_FALSE(telebezel::token_matches("secret-a", "secret-b"));
  std::array<std::uint8_t, 32> key{};
  for (std::size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<std::uint8_t>(i);
  const auto derived = telebezel::derive_database_key(key, "00112233-4455-4677-8899-aabbccddeeff");
  EXPECT_EQ(telebezel::hex_encode(derived.data(), derived.size()),
            "dffbc48560404bbdf166f3015cfd8fa61ac4cf9fdab4256af783bdda22046181");
}
TEST(RegistryIdentity, AcceptsLaravelUuidsAndRejectsPaths) {
  EXPECT_TRUE(telebezel::valid_uuid("00112233-4455-4677-8899-aabbccddeeff"));
  EXPECT_TRUE(telebezel::valid_uuid("019966a1-2345-7123-8123-123456789abc"));
  EXPECT_FALSE(telebezel::valid_uuid("../../escape"));
  EXPECT_FALSE(telebezel::valid_uuid("00112233-4455-9677-8899-aabbccddeeff"));
}
class StatusContract : public ::testing::TestWithParam<bool> {};
TEST_P(StatusContract, MatchesSharedJsonFixture) {
  const bool ready = GetParam();
  const auto fixture = ready ? "tdlib_status_ready.json" : "tdlib_status_not_ready.json";
  std::ifstream stream(std::string(TELEBEZEL_CONTRACT_FIXTURE_DIR) + "/" + fixture);
  ASSERT_TRUE(stream);
  nlohmann::json expected;
  stream >> expected;
  telebezel::StatusSnapshot snapshot{ready, ready ? "1.8.67" : "", 0};
  EXPECT_EQ(nlohmann::json::parse(telebezel::status_json(snapshot, "fixture-request-id")), expected);
}
INSTANTIATE_TEST_SUITE_P(Readiness, StatusContract, ::testing::Bool());
TEST(Config, ValidatesPortsSecretFilesAndProxyCredentials) {
  Environment env;
  env.set("TDLIB_INTERNAL_TOKEN", "0123456789abcdef0123456789abcdef");
  env.set("TDLIB_LISTEN_PORT", "8081junk");
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_LISTEN_PORT", std::nullopt);
  env.set("TDLIB_INTERNAL_TOKEN_FILE", "/nonexistent-telebezel-secret");
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_INTERNAL_TOKEN_FILE", std::nullopt);
  env.set("TDLIB_PROXY_MODE", "socks5");
  env.set("TDLIB_PROXY_HOST", "proxy.example");
  env.set("TDLIB_PROXY_PORT", "1080");
  EXPECT_EQ(telebezel::load_config().proxy.port, 1080);
  env.set("TDLIB_PROXY_MODE", "http");
  env.set("TDLIB_PROXY_HTTP_ONLY", "true");
  EXPECT_TRUE(telebezel::load_config().proxy.http_only);
  env.set("TDLIB_PROXY_HTTP_ONLY", std::nullopt);
  env.set("TDLIB_PROXY_MODE", "mtproto");
  env.set("TDLIB_PROXY_SECRET", std::nullopt);
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_PROXY_SECRET", "not-logged-secret");
  EXPECT_EQ(telebezel::load_config().proxy.mode, telebezel::ProxyMode::mtproto);
  env.set("TDLIB_PROXY_SECRET", std::nullopt);
  env.set("TDLIB_PROXY_MODE", std::nullopt);
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
}
TEST_F(RegistryTest, SecretFilesAreTrimmedAndMustBeSafe) {
  const auto secret = root / "secret";
  const auto write = [&](const std::string &content, std::filesystem::perms permissions) {
    std::filesystem::remove(secret);
    {
      std::ofstream output(secret, std::ios::binary);
      output << content;
    }
    std::filesystem::permissions(secret, permissions, std::filesystem::perm_options::replace);
  };
  const auto owner = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
  write("0123456789abcdef0123456789abcdef\r\n", owner);
  EXPECT_EQ(telebezel::read_secret_file(secret.string(), "token"), "0123456789abcdef0123456789abcdef");
  write("\n", owner);
  EXPECT_THROW(telebezel::read_secret_file(secret.string(), "token"), std::runtime_error);
  write("first\nsecond\n", owner);
  EXPECT_THROW(telebezel::read_secret_file(secret.string(), "token"), std::runtime_error);
  write("value\n", owner | std::filesystem::perms::others_read);
  EXPECT_THROW(telebezel::read_secret_file(secret.string(), "token"), std::runtime_error);
  write("value\n", owner | std::filesystem::perms::group_read);
  EXPECT_EQ(telebezel::read_secret_file(secret.string(), "token"), "value");
  const auto link = root / "secret-link";
  std::filesystem::create_symlink(secret, link);
  EXPECT_THROW(telebezel::read_secret_file(link.string(), "token"), std::runtime_error);
  EXPECT_THROW(telebezel::read_secret_file(root.string(), "token"), std::runtime_error);
}
TEST_F(RegistryTest, ConfigurationLimitsAndMasterKeyAreCheckedAtStartup) {
  Environment env;
  env.set("TDLIB_INTERNAL_TOKEN", "0123456789abcdef0123456789abcdef");
  env.set("TDLIB_DATABASE_MASTER_KEY_FILE", master.string());
  EXPECT_NO_THROW(telebezel::load_config());
  env.set("TDLIB_PREVIEW_MAX_BYTES", "2048");
  env.set("TDLIB_PREVIEW_TOTAL_BYTES", "1024");
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_PREVIEW_MAX_BYTES", std::nullopt);
  env.set("TDLIB_PREVIEW_TOTAL_BYTES", std::nullopt);
  env.set("TDLIB_CACHE_MESSAGES_PER_CHAT", "600");
  env.set("TDLIB_CACHE_MESSAGES_PER_ACCOUNT", "500");
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_CACHE_MESSAGES_PER_CHAT", std::nullopt);
  env.set("TDLIB_CACHE_MESSAGES_PER_ACCOUNT", "30000");
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_CACHE_MESSAGES_PER_ACCOUNT", std::nullopt);
  env.set("TDLIB_UPDATES_WAIT_MAX_SECONDS", "26");
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_UPDATES_WAIT_MAX_SECONDS", "20");
  EXPECT_EQ(telebezel::load_config().updates_wait_max_seconds, 20U);
  std::filesystem::permissions(master, std::filesystem::perms::others_read, std::filesystem::perm_options::add);
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
  env.set("TDLIB_DATABASE_MASTER_KEY_FILE", (root / "missing").string());
  EXPECT_THROW(telebezel::load_config(), std::runtime_error);
}
TEST_F(RegistryTest, MasterKeyPermissions) {
  EXPECT_EQ(telebezel::read_master_key(master)[0], 0);
  std::filesystem::permissions(master, std::filesystem::perms::group_read, std::filesystem::perm_options::add);
  EXPECT_EQ(telebezel::read_master_key(master)[0], 0);
  std::filesystem::permissions(master, std::filesystem::perms::others_read, std::filesystem::perm_options::add);
  EXPECT_THROW(telebezel::read_master_key(master), std::runtime_error);
}
TEST_F(RegistryTest, PersistsEncryptedManifestAndRemovesStorage) {
  telebezel::Registry registry(root, master);
  registry.open();
  const std::string uuid = "00112233-4455-4677-8899-aabbccddeeff";
  const std::string generation = "11112233-4455-4677-8899-aabbccddeeff";
  registry.ensure_account_directories(uuid);
  ASSERT_TRUE(registry.account_directory_exists(uuid));
  registry.write({1, uuid, generation, false, 1, 7, "active", "", false, nullptr, "", ""},
                 {{"mode", "direct"}, {"http_only", false}});
  auto manifest = registry.read(uuid);
  ASSERT_TRUE(manifest);
  EXPECT_EQ(manifest->generation, generation);
  EXPECT_EQ(manifest->revision, 7);
  std::ifstream stored(root / "registry" / (uuid + ".json"));
  const std::string encoded((std::istreambuf_iterator<char>(stored)), std::istreambuf_iterator<char>());
  EXPECT_NE(encoded.find("proxy_protected"), std::string::npos);
  EXPECT_EQ(encoded.find("\"proxy\""), std::string::npos);
  EXPECT_EQ(encoded.find("direct"), std::string::npos);
  registry.remove_account_directory(uuid);
  EXPECT_FALSE(registry.account_directory_exists(uuid));
}
TEST_F(RegistryTest, DetectsOrphansCorruptionAndUnsafeDeletion) {
  telebezel::Registry registry(root, master);
  registry.open();
  const std::string orphan = "20112233-4455-4677-8899-aabbccddeeff";
  const std::string corrupt = "30112233-4455-4677-8899-aabbccddeeff";
  registry.ensure_account_directories(orphan);
  EXPECT_EQ(registry.orphan_account_ids(), std::vector<std::string>{orphan});
  {
    std::ofstream output(root / "registry" / (corrupt + ".json"));
    output << "not-json";
  }
  std::filesystem::permissions(root / "registry" / (corrupt + ".json"),
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
  EXPECT_TRUE(registry.discover().empty());
  EXPECT_EQ(registry.manifest_errors().at(corrupt), "storage.corrupt");
  std::filesystem::create_symlink(root / "registry" / (corrupt + ".json"),
                                  root / "accounts" / orphan / "db" / "unsafe-link");
  try {
    registry.remove_account_directory(orphan);
    FAIL() << "Unsafe deletion accepted";
  } catch (const std::runtime_error &error) {
    EXPECT_STREQ(error.what(), "storage.unsafe_path");
  }
  EXPECT_TRUE(registry.account_directory_exists(orphan));
}
TEST_F(RegistryTest, RejectsSecondVolumeOwner) {
  telebezel::Registry registry(root, master);
  registry.open();
  telebezel::Registry second(root, master);
  EXPECT_THROW(second.open(), std::runtime_error);
}
} // namespace
