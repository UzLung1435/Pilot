import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pilot_tool.spec import patch_image, validate_spec
from pilot_tool.ram import generate_module
from pilot_tool.cli import main as cli_main


class PatchTests(unittest.TestCase):
    def setUp(self):
        self.before = b"original image"
        self.after = b"ORiginal image"
        self.spec = {"schema": "pilot.patch.v1", "id": "synthetic", "kind": "file",
                     "target": {"model": "test", "firmware": "test", "component": "test.bin",
                                "sha256": hashlib.sha256(self.before).hexdigest(),
                                "patched_sha256": hashlib.sha256(self.after).hexdigest(), "size_bytes": len(self.before)},
                     "patches": [{"id": "first", "offset": 0, "before": "6f72", "after": "4f52"}]}

    def test_apply_restore_description_immutable(self):
        original = copy.deepcopy(self.spec)
        self.assertEqual(patch_image(self.spec, self.before), self.after)
        self.assertEqual(patch_image(self.spec, self.after, True), self.before)
        self.assertEqual(original, self.spec)

    def test_whole_image_and_expected_bytes_required(self):
        with self.assertRaises(ValueError):
            patch_image(self.spec, b"bad image")
        self.spec["patches"][0]["before"] = "0000"
        with self.assertRaises(ValueError):
            patch_image(self.spec, self.before)

    def test_bad_result_hash_and_overlap(self):
        self.spec["target"]["patched_sha256"] = "0"*64
        with self.assertRaises(ValueError):
            patch_image(self.spec, self.before)
        self.spec["patches"].append({"id": "second", "offset": 1, "before": "72", "after": "52"})
        with self.assertRaises(ValueError):
            validate_spec(self.spec)

    def test_reject_unknown_fields_and_unsafe_addresses(self):
        self.spec["target"]["private_path"] = "not a public description field"
        with self.assertRaises(ValueError):
            validate_spec(self.spec)
        del self.spec["target"]["private_path"]
        self.spec["patches"][0]["offset"] = True
        with self.assertRaises(ValueError):
            validate_spec(self.spec)

    def test_ram_source_checks_identity_and_preserves_offsets(self):
        spec = copy.deepcopy(self.spec)
        spec["kind"] = "ram"
        spec["target"].pop("patched_sha256")
        spec["target"]["component"] = "appFw.so"
        spec["patches"] = [{"id": "ap_site", "space": "ap", "address": "0x100", "before": "0000", "after": "0100"},
                           {"id": "cp_site", "space": "cp", "address": "0x1000000", "before": "00", "after": "03"}]
        source = generate_module(spec)
        self.assertIn('pilot_sha256_file("/usr/lib/appFw.so"', source)
        self.assertIn("PILOT_PATCH_SPACE_CP", source)
        self.assertIn("UINT64_C(256)", source)
        self.assertEqual(spec["patches"][0]["address"], "0x100")

    def test_cli_never_overwrites_existing_image(self):
        cli = Path(__file__).resolve().parents[1] / "pilot.py"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/"spec.json").write_text(json.dumps(self.spec), encoding="utf-8")
            (root/"image.bin").write_bytes(self.before)
            result = subprocess.run([sys.executable, str(cli), "image", str(root/"spec.json"), str(root/"image.bin"), "--out", str(root/"image.bin")], capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual((root/"image.bin").read_bytes(), self.before)

    def test_cp_code_address_and_patcher_region(self):
        spec = copy.deepcopy(self.spec)
        spec["kind"] = "ram"
        spec["target"].pop("patched_sha256")
        spec["target"]["component"] = "appFw.so"
        spec["patches"] = [{"id": "cp", "space": "cp", "address": "0x128000", "before": "00", "after": "01"}]
        validate_spec(spec)
        spec["patches"][0]["address"] = "0xC12D68"
        with self.assertRaises(ValueError):
            validate_spec(spec)

    def test_cp_flash_uses_camera_entry(self):
        with mock.patch("pilot_tool.cli.subprocess.run") as run:
            run.return_value.returncode = 0
            self.assertEqual(cli_main(["flash", "cp-install", "--yes"]), 0)
            self.assertIn("--cp-install", run.call_args.args[0])
            self.assertIn("--yes", run.call_args.args[0])
            self.assertTrue(run.call_args.args[0][1].endswith("patch\\on_camera.py") or
                            run.call_args.args[0][1].endswith("patch/on_camera.py"))

    def test_ap_prepare_forwards_plan_paths(self):
        with mock.patch("pilot_tool.cli.subprocess.run") as run:
            run.return_value.returncode = 0
            self.assertEqual(cli_main(["flash", "prepare-ap", "--pilot", "pilot.so", "--output-dir", "plan"]), 0)
            self.assertEqual(run.call_args.args[0][2:], ["--prepare-ap", "--pilot", "pilot.so", "--output-dir", "plan"])

    def test_flash_install_forwards_force(self):
        with mock.patch("pilot_tool.cli.subprocess.run") as run:
            run.return_value.returncode = 0
            self.assertEqual(cli_main(["flash", "install", "--yes", "--force"]), 0)
            self.assertEqual(run.call_args.args[0][2:], ["--install", "--yes", "--force"])


if __name__ == "__main__":
    unittest.main()
