#ifndef TELEBEZEL_LOCALIZATION_H
#define TELEBEZEL_LOCALIZATION_H

typedef struct {
  const char *title;
  const char *checking;
  const char *connected;
  const char *config_missing;
  const char *config_invalid;
  const char *backend_unavailable;
  const char *api_unauthorized;
  const char *backend_not_ready;
  const char *protocol_error;
} TbStrings;

const TbStrings *tb_localization_current(void);

#endif
