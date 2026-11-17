"""上传完整性、输出隔离及合成 RP9 容器回归测试。"""
import gzip
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rpformat as rf
import rp_server as server
from rp_viewer import Viewer


def minimal_rp():
    def words(*values):
        return struct.pack('<' + 'I' * len(values), *values)
    name = b'Axure:Page'
    blob = (words(27, 9, 0, 1, 0, 0xC351, 31, 1, len(name)) + name
            + words(30, 0, *rf.MAGIC_CONSTS, 0, 28))
    payload = gzip.compress(blob)
    return b'\xac\xef\x09\x00' + bytes(12) + words(0, len(payload)) + payload


class ViewerTests(unittest.TestCase):
    def test_multipart_preserves_trailing_bytes(self):
        for payload in (b'abc\r\n', b'abc\n', b'abc\r', b'abc'):
            body = (b'--test\r\nContent-Disposition: form-data; name="file"; '
                    b'filename="test.rp"\r\nContent-Type: application/octet-stream\r\n\r\n'
                    + payload + b'\r\n--test--\r\n')
            self.assertEqual(server.parse_upload('multipart/form-data; boundary=test', body),
                             ('test.rp', payload))

    def test_static_site_and_separate_uploads(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]) as tmp:
            with patch.object(server, 'OUT_ROOT', tmp):
                first = server.parse_bytes('demo.rp', minimal_rp())
                second = server.parse_bytes('demo.rp', minimal_rp())
                self.assertTrue(first['ok'], first)
                self.assertTrue(second['ok'], second)
                self.assertEqual(first['pages'], 1)
                self.assertNotEqual(first['url'], second['url'])
                self.assertFalse(any(p.name.startswith('_') for p in Path(tmp).iterdir()))
                for site in Path(tmp).iterdir():
                    self.assertTrue((site / 'pages/p0.html').is_file())
                    self.assertEqual(len(json.loads((site / 'pages.json').read_text())), 1)
                self.assertFalse(server.parse_bytes('broken.rp', b'broken')['ok'])
                self.assertEqual(len(list(Path(tmp).iterdir())), 2)

    def test_unknown_tag_and_negative_read_rejected(self):
        ctx = rf.Ctx([], [], [], 'test')
        with self.assertRaises(rf.ParseError):
            rf.parse_value(rf.Reader(struct.pack('<III', 999, 1, 2)), ctx)
        with self.assertRaises(rf.ParseError):
            rf.Reader(b'abc').bytes(-1)

    def test_image_extension_cannot_escape_directory(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]) as tmp:
            viewer = Viewer('unused.rp', tmp)
            self.assertIsNone(viewer.save_image({'Stream': b'x', 'Extension': '/../evil.html'}))
            self.assertEqual(list(Path(tmp).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
