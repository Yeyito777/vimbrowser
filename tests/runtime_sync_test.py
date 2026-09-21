#!/usr/bin/env python3
"""Runtime identity regression; no browser/display is launched."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


def build_id(path):
    output = subprocess.check_output(['readelf', '-n', str(path)], text=True)
    return next(line.split('Build ID:')[1].strip() for line in output.splitlines() if 'Build ID:' in line)


class RuntimeSyncTest(unittest.TestCase):
    def test_newer_mtime_cannot_hide_an_older_backend(self):
        with tempfile.TemporaryDirectory(prefix='vb-runtime-sync-') as tmp:
            root = Path(tmp)
            src, dst = root/'source', root/'runtime'
            src.mkdir()
            dst.mkdir()
            for directory, value in ((src, 2), (dst, 1)):
                source = directory/'fixture.c'
                source.write_text(f'int fixture_version(void) {{ return {value}; }}\n')
                subprocess.run(['cc', '-shared', '-fPIC', '-Wl,--build-id', str(source), '-o', str(directory/'libcef.so')], check=True)
            expected = build_id(src/'libcef.so')
            self.assertNotEqual(expected, build_id(dst/'libcef.so'))
            future = time.time()+3600
            os.utime(dst/'libcef.so', (future, future))
            old_inode = (dst/'libcef.so').stat().st_ino
            with (dst/'libcef.so').open('rb') as existing_mapping:
                old_payload = existing_mapping.read()
                subprocess.run([str(ROOT/'scripts/sync-chromium-runtime.sh'), str(src), str(dst)], check=True, capture_output=True)
                self.assertEqual(expected, build_id(dst/'libcef.so'))
                self.assertNotEqual(old_inode, (dst/'libcef.so').stat().st_ino)
                existing_mapping.seek(0)
                self.assertEqual(old_payload, existing_mapping.read())
            # An identity match is a real no-op, independent of timestamps.
            inode = (dst/'libcef.so').stat().st_ino
            subprocess.run([str(ROOT/'scripts/sync-chromium-runtime.sh'), str(src), str(dst)], check=True, capture_output=True)
            self.assertEqual(inode, (dst/'libcef.so').stat().st_ino)


if __name__ == '__main__':
    unittest.main()
