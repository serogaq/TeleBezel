#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import unittest
from pathlib import Path

script = Path(__file__).resolve().parents[2] / '.github/scripts/repebble_draft.py'
spec = importlib.util.spec_from_file_location('repebble_draft', script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class DraftUploadTest(unittest.TestCase):
    def setUp(self):
        self.pbw = b'approved-pbw-bytes'
        self.digest = hashlib.sha256(self.pbw).hexdigest()
        self.files = [('pbwFile', ('app.pbw', io.BytesIO(self.pbw), 'application/octet-stream'))]

    def test_new_app_payload_is_invisible_and_exact_bytes(self):
        sent = module.guard_upload('https://appstore-api.repebble.com/api/dashboard/apps',
            {'visible': 'true', 'isPublished': 'true'}, self.files, self.digest)
        self.assertEqual(sent, {'visible': 'false', 'isPublished': 'false'})

    def test_new_release_payload_is_unpublished_and_exact_bytes(self):
        sent = module.guard_upload('https://appstore-api.repebble.com/api/dashboard/apps/123/releases',
            {'version': '1.0.0', 'isPublished': 'true'}, self.files, self.digest)
        self.assertEqual(sent['isPublished'], 'false')

    def test_modified_pbw_or_unknown_payload_fails_before_http_write(self):
        with self.assertRaisesRegex(RuntimeError, 'different PBW'):
            module.guard_upload('https://appstore-api.repebble.com/api/dashboard/apps',
                {'visible': 'true', 'isPublished': 'true'}, self.files, hashlib.sha256(b'other').hexdigest())
        with self.assertRaisesRegex(RuntimeError, 'payload changed'):
            module.guard_upload('https://appstore-api.repebble.com/api/dashboard/apps',
                {'isPublished': 'true'}, self.files, self.digest)
        with self.assertRaisesRegex(RuntimeError, 'Unexpected'):
            module.guard_upload('https://unknown.example/api/dashboard/apps',
                {'visible': 'true', 'isPublished': 'true'}, self.files, self.digest)

    def test_unknown_tool_version_and_source_fail_before_http_write(self):
        with self.assertRaisesRegex(RuntimeError, 'Unknown'):
            module.guard_publisher_source('5.0.41', script)
        with self.assertRaisesRegex(RuntimeError, 'Unknown'):
            module.guard_publisher_source('5.0.40', script)

if __name__ == '__main__':
    unittest.main()
