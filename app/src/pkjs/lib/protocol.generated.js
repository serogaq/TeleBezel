'use strict';

module.exports = {
  "request": {
    "status": 1,
    "hello": 2
  },
  "response": {
    "status": 1,
    "ready": 2,
    "refresh": 3
  },
  "result": {
    "ok": 0,
    "config_missing": 1,
    "config_invalid": 2,
    "backend_unavailable": 3,
    "api_unauthorized": 4,
    "backend_not_ready": 5,
    "protocol_error": 6
  }
};
