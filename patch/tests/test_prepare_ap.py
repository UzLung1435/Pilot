import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_ap", ROOT / "patch/prepare_ap.py")
prepare_ap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare_ap)


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.image = bytearray(256)
        self.image[:4] = b"\x7fELF"
        self.image[32:36] = b"DIFF"
        self.pilot = bytearray(64)
        self.pilot[:6] = b"\x7fELF\x02\x01"
        self.pilot[18:20] = (183).to_bytes(2, "little")
        self.writer = bytearray(128)
        self.writer[:6] = b"\x7fELF\x02\x01"
        self.writer[18:20] = (183).to_bytes(2, "little")
        self.writer[64:84] = b"pilot_ap_generic_v1\0"
        self.config = {"schema": "pilot.patch-map.v1", "target": {"model": "test", "firmware": "test", "binary_size": 256}, "patches": [
            {"id": name, "file_offset": hex(offset), "length": 4, "original_bytes": "00000000", "target_bytes": "01020304"}
            for name, offset in zip(prepare_ap.WINDOW_IDS, (32, 64, 96, 128))]}

    def test_target_specific_plan_and_report(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "plan"
            result = prepare_ap.prepare(self.config, bytes(self.image), bytes(self.pilot), out, bytes(self.writer))
            plan = json.loads((out / "patch.json").read_text(encoding="utf-8"))
            self.assertEqual(plan["patches"][0]["original_bytes"], b"DIFF".hex())
            self.assertEqual(result["windows"][0]["state"], "different")
            candidate = bytearray(self.image)
            for offset in (32, 64, 96, 128):
                candidate[offset:offset + 4] = bytes.fromhex("01020304")
            self.assertEqual(plan["target"]["patched_binary_sha256"], hashlib.sha256(candidate).hexdigest())
            self.assertEqual((out / "appFw.before.so").read_bytes(), self.image)
            self.assertEqual((out / "patch.bin").read_bytes(), prepare_ap.plan_from_config(plan))

    def test_overlapping_windows_are_rejected(self):
        self.config["patches"][1]["file_offset"] = "0x20"
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "overlapping"):
                prepare_ap.prepare(self.config, bytes(self.image), bytes(self.pilot), Path(temp) / "plan", bytes(self.writer))

    def test_already_installed_requires_no_writer(self):
        for offset in (32, 64, 96, 128):
            self.image[offset:offset + 4] = bytes.fromhex("01020304")
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "plan"
            result = prepare_ap.prepare(self.config, bytes(self.image), bytes(self.pilot), out, bytes(self.writer))
            self.assertTrue(result["already_installed"])
            self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
