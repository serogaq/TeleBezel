#!/usr/bin/env python3
"""Fail-closed, byte-identical, invisible upload using pinned pebble-tool 5.0.40.

The upstream publish command rebuilds and hard-codes publication flags. Keep
the dependency intact and guard its only multipart write boundary instead.
"""
import hashlib
import importlib.metadata
import json
import os
import re
import sys
import zipfile
from pathlib import Path

EXPECTED_SOURCE_SHA256 = "465bbebcab01e6ba1ad03e05b541d9c258f641e142f7afc4f1c30e896723d7dc"
EXPECTED_TOOL_VERSION = "5.0.40"

def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def guard_publisher_source(version, path):
    if version != EXPECTED_TOOL_VERSION or sha256(path) != EXPECTED_SOURCE_SHA256:
        raise RuntimeError("Unknown pebble-tool version/source; no upload permitted")

def guard_upload(url, data, files, expected_sha):
    if not re.fullmatch(r"https://appstore-api\.repebble\.com/api/dashboard/apps(?:/[^/]+/releases)?", url):
        raise RuntimeError("Unexpected RePebble upload endpoint")
    if "isPublished" not in data:
        raise RuntimeError("Upstream publication payload changed")
    data = dict(data)
    data["isPublished"] = "false"
    if url.endswith("/apps"):
        if "visible" not in data:
            raise RuntimeError("Upstream app visibility payload changed")
        data["visible"] = "false"
    pbws = [item for item in files if item[0] == "pbwFile"]
    if len(pbws) != 1:
        raise RuntimeError("Expected exactly one PBW multipart part")
    handle = pbws[0][1][1]
    position = handle.tell()
    handle.seek(0)
    actual = hashlib.sha256(handle.read()).hexdigest()
    handle.seek(position)
    if actual != expected_sha:
        raise RuntimeError("Publisher attempted to upload different PBW bytes")
    return data

def verify_pbw(path, expected_sha, expected_version):
    if sha256(path) != expected_sha:
        raise RuntimeError("Downloaded PBW checksum mismatch")
    with zipfile.ZipFile(path) as archive:
        info = json.loads(archive.read("appinfo.json"))
    if info.get("uuid") != str(info.get("uuid", "")).lower():
        raise RuntimeError("Publisher would normalize PBW UUID and change approved bytes")
    if str(info.get("versionLabel")) != expected_version:
        raise RuntimeError("PBW version differs from release version")

def main():
    version = importlib.metadata.version("pebble-tool")
    from pebble_tool.commands import publish
    source = Path(publish.__file__)
    guard_publisher_source(version, source)

    expected_sha = os.environ["TELEBEZEL_EXPECTED_PBW_SHA256"]
    expected_version = os.environ["TELEBEZEL_RELEASE_VERSION"]
    root = Path(__file__).resolve().parents[2]
    pbw_path = root / "app/build/app.pbw"
    verify_pbw(pbw_path, expected_sha, expected_version)

    from pebble_tool.exceptions import ToolError
    original_post = publish.PublishCommand._post_with_wait_bar.__func__
    responses = []

    def guarded_post(cls, url, headers, data, files, timeout, label):
        try:
            safe_data = guard_upload(url, data, files, expected_sha)
        except RuntimeError as exc:
            raise ToolError(str(exc)) from exc
        response = original_post(cls, url, headers, safe_data, files, timeout, label)
        responses.append((url, response))
        return response

    def no_build(self, args):
        verify_pbw(pbw_path, expected_sha, expected_version)

    def no_developer_create(cls, api_base, firebase_id_token):
        raise ToolError("Developer account must already be linked before release staging")

    publish.PublishCommand._post_with_wait_bar = classmethod(guarded_post)
    publish.PublishCommand._build_project = no_build
    publish.PublishCommand._create_developer = classmethod(no_developer_create)

    from pebble_tool import run_tool
    args = sys.argv[1:]
    if "--is-published" in args or "--replace-screenshots" in args:
        raise RuntimeError("Unsafe publication flags are prohibited")
    os.chdir(root / "app")
    run_tool(["publish", *args])
    if len(responses) != 1:
        raise RuntimeError("Expected one guarded RePebble upload")

    # Never assert staging success from HTTP 2xx alone. Re-fetch developer
    # state and require explicit private fields; unknown response shapes fail.
    import requests
    token = os.environ["PEBBLE_FIREBASE_ID_TOKEN"]
    upload_payload = responses[0][1].json()
    app_id = publish.PublishCommand._extract_app_id(upload_payload)
    if not app_id:
        with zipfile.ZipFile(pbw_path) as archive:
            uuid = json.loads(archive.read("appinfo.json"))["uuid"]
        me = publish.PublishCommand._get_me_context("https://appstore-api.repebble.com", token)
        lookup = ((me or {}).get("app_lookup") or {}).get("by_app_uuid") or {}
        app_id = publish.PublishCommand._lookup_app_id_case_insensitive(lookup, uuid)
    if not app_id:
        raise RuntimeError("Could not verify staged RePebble app identity")
    detail = requests.get(f"https://appstore-api.repebble.com/api/dashboard/apps/{app_id}",
                          headers={"Authorization": f"Bearer {token}"}, timeout=15)
    detail.raise_for_status()
    body = detail.json()
    app = body.get("app", body.get("data", body))
    if not isinstance(app, dict):
        raise RuntimeError("RePebble did not return app state")
    if responses[0][0].endswith("/apps") and app.get("visible") is not False:
        raise RuntimeError("RePebble did not confirm invisible new app state")
    releases = app.get("releases")
    if not isinstance(releases, list) or not any(
        isinstance(item, dict) and item.get("version") == expected_version and item.get("isPublished") is False
        for item in releases
    ):
        raise RuntimeError("RePebble did not confirm unpublished release state")

if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"RePebble draft staging failed closed: {error}", file=sys.stderr)
        sys.exit(1)
