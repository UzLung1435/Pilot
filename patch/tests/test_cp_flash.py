import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
module_spec = importlib.util.spec_from_file_location("cp_flash", ROOT / "patch/cp_flash.py")
cp_flash = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(cp_flash)


class FakeSession:
    def __init__(self, image):
        self.files = {cp_flash.CP_PATH: image}
        self.path = None

    def select(self, path):
        self.path = path

    def read_path(self, path, missing=False):
        return self.files.get(path)

    def read(self, name, missing=False):
        return self.read_path("/tmp/jiritsu/" + name, missing)

    def jiritsu(self, command, data, missing=False):
        if command == 18:
            self.files.pop(self.path, None)
            return
        self.files[self.path] = data[3:]
        if self.path.endswith("/libJit.so"):
            delta = self.read("cp.delta")
            _, _, count, _, _ = struct.unpack_from("<8sIIQQ", delta)
            base = 96 + count * 16
            output = bytearray(self.files[cp_flash.CP_PATH])
            restoring = self.read("cpdelta.mode") == b"restore\n"
            for i in range(count):
                offset, length, rel = struct.unpack_from("<QII", delta, 96 + i * 16)
                start = base + rel + (0 if restoring else length)
                output[offset:offset + length] = delta[start:start + length]
            self.files[cp_flash.CP_PATH] = bytes(output)
            self.files["/tmp/jiritsu/cpdelta.result"] = b"A6700CPDELTA\nRESULT=OK\n"


class CpFlashTests(unittest.TestCase):
    def test_install_restore_and_backups(self):
        spec = {"target": {"size_bytes": 6}, "patches": [
            {"offset": 1, "before": "6263", "after": "7879"}]}
        session = FakeSession(b"abcdef")
        with tempfile.TemporaryDirectory() as temp:
            cp_flash.run(session, spec, "install", Path(temp))
            self.assertEqual(session.files[cp_flash.CP_PATH], b"axydef")
            self.assertEqual(len(list(Path(temp).glob("*.bin"))), 1)
            cp_flash.run(session, spec, "restore", Path(temp))
            self.assertEqual(session.files[cp_flash.CP_PATH], b"abcdef")
            self.assertEqual(len(list(Path(temp).glob("*.bin"))), 2)

    def test_refuses_unrecognized_patch(self):
        spec = {"target": {"size_bytes": 6}, "patches": [
            {"offset": 1, "before": "6263", "after": "7879"}]}
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, "unknown"):
                cp_flash.run(FakeSession(b"aZZdef"), spec, "install", Path(temp))


if __name__ == "__main__":
    unittest.main()
