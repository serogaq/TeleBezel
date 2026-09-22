#!/usr/bin/env python3
import json
import sys
import zipfile
from pathlib import Path

EXPECTED_UUID = "b91f715e-af74-4a90-9df4-fda0fbcd9762"
EXPECTED_PLATFORMS = {"diorite", "emery", "flint", "gabbro"}
EXPECTED_KEYS = {
    "REQUEST_KIND", "REQUEST_SEQ", "RESPONSE_KIND", "RESULT_CODE", "INBOX_SIZE", "PAYLOAD", "ENTITY_ID",
    "CHUNK_INDEX", "CHUNK_TOTAL", "CONFIG_ADDRESS", "CONFIG_SSL", "CONFIG_TOKEN",
}

# Pebble SDK's STM32 CRC implementation is Apache-2.0 (Google LLC, 2024).
# The bundle manifest uses this CRC, not ZIP's CRC-32.
def stm32_crc(data):
    polynomial = 0x04C11DB7
    table = []
    for value in range(256):
        register = value << 24
        for _ in range(8):
            register = ((register << 1) ^ polynomial) if register & 0x80000000 else register << 1
        table.append(register & 0xFFFFFFFF)
    crc = 0xFFFFFFFF
    for offset in range(0, len(data), 4):
        word = data[offset:offset + 4]
        if len(word) < 4:
            word = word[::-1] + bytes(4 - len(word))
        for byte in reversed(word):
            crc = ((crc << 8) ^ table[(crc >> 24) ^ byte]) & 0xFFFFFFFF
    return crc

def require(condition, message):
    if not condition:
        raise ValueError(message)

pbw = Path(sys.argv[1])
with zipfile.ZipFile(pbw) as archive:
    require(archive.testzip() is None, "ZIP member integrity failure")
    names = set(archive.namelist())
    info = json.loads(archive.read("appinfo.json"))
    require(info["uuid"] == EXPECTED_UUID, "wrong application UUID")
    require(set(info["targetPlatforms"]) == EXPECTED_PLATFORMS, "wrong platforms")
    require(set(info["messageKeys"]) == EXPECTED_KEYS, "wrong AppMessage keys")
    expected_version = json.loads(Path("app/package.json").read_text())["version"]
    require(info["versionLabel"] == expected_version, "PBW version drift")
    require("pebble-js-app.js" in names, "missing PKJS")
    js = archive.read("pebble-js-app.js")
    require(len(js) > 0, "empty PKJS")
    for locale in ("en", "ru"):
        values = json.loads(Path(f"app/localization/{locale}/settings.json").read_text())
        require(all(str(value).encode("utf-8") in js for value in values.values()),
                f"missing {locale} PKJS localization")
    for platform in EXPECTED_PLATFORMS:
        manifest_name = f"{platform}/manifest.json"
        require(manifest_name in names, f"missing {platform} manifest")
        manifest = json.loads(archive.read(manifest_name))
        require(manifest.get("manifestVersion") == 2 and manifest.get("type") == "application",
                f"invalid {platform} manifest")
        for field, filename in (("application", "pebble-app.bin"), ("resources", "app_resources.pbpack")):
            path = f"{platform}/{filename}"
            require(path in names and manifest[field]["name"] == filename, f"missing {path}")
            payload = archive.read(path)
            require(len(payload) > 0 and manifest[field]["size"] == len(payload), f"size mismatch: {path}")
            require(manifest[field]["crc"] == stm32_crc(payload), f"STM32 CRC mismatch: {path}")
        watch_strings = []
        for locale in ("en", "ru"):
            watch_strings.extend(json.loads(Path(f"app/localization/{locale}/watch.json").read_text()).values())
        binary = archive.read(f"{platform}/pebble-app.bin")
        require(all(str(value).encode("utf-8") in binary for value in watch_strings),
                f"missing watch localization in {platform}")
print(f"validated {pbw}")
