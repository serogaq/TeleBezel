#include <pebble.h>
#include <string.h>
#include "localization.h"

static const TbStrings s_en = {
  .title = "TeleBezel",
  .checking = "Checking…",
  .connected = "Connected",
  .config_missing = "Open settings",
  .config_invalid = "Invalid settings",
  .backend_unavailable = "Backend unavailable",
  .api_unauthorized = "Invalid API token",
  .backend_not_ready = "Backend not ready",
  .protocol_error = "Protocol error",
};

static const TbStrings s_ru = {
  .title = "TeleBezel",
  .checking = "Проверка…",
  .connected = "Подключено",
  .config_missing = "Откройте настройки",
  .config_invalid = "Ошибка настроек",
  .backend_unavailable = "Сервер недоступен",
  .api_unauthorized = "Неверный API-токен",
  .backend_not_ready = "Сервер не готов",
  .protocol_error = "Ошибка протокола",
};

const TbStrings *tb_localization_current(void) {
  const char *locale = i18n_get_system_locale();
  return locale && strncmp(locale, "ru", 2) == 0 ? &s_ru : &s_en;
}
