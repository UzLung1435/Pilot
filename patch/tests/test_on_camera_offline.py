from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path


PATCH_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PATCH_DIR))
from on_camera_config import add_path_arguments, resolve_paths


def load_on_camera():
    spec = importlib.util.spec_from_file_location(
        "pilot_on_camera", PATCH_DIR / "on_camera.py")
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(module)
    return module


class OnCameraOfflineTests(unittest.TestCase):
    def test_backup_preserves_exact_bytes_and_absent_files(self):
        module = load_on_camera()
        with tempfile.TemporaryDirectory() as directory:
            module.STATE_DIR = Path(directory)
            backup = module.backup_device_files({"appFw.so": b"before image", "pilot.so": None})
            self.assertEqual((backup / "appFw.so").read_bytes(), b"before image")
            self.assertIsNone(json.loads((backup / "metadata.json").read_text())["pilot.so"])
            self.assertFalse((backup / "pilot.so").exists())

    def test_missing_bootstrap_is_actually_installed_for_existing_ap_patch(self):
        from unittest.mock import Mock, patch
        module = load_on_camera()
        session = Mock()
        session.read_path.return_value = None
        with patch.object(module, "read_ap", return_value=b"patched"), \
             patch.object(module, "ap_state", return_value="patched"), \
             patch.object(module, "ensure_no_flash_files"), \
             patch.object(module, "stage_pilot_dso", return_value=True), \
             patch.object(module, "run_update_pilot", return_value=0) as update:
            self.assertEqual(module.run_write(session, {}, b"writer", b"pilot", "write"), 0)
            update.assert_called_once_with(session, {}, b"writer", b"pilot")

    def test_explicit_local_paths(self):
        import argparse

        parser = argparse.ArgumentParser()
        add_path_arguments(parser)
        args = parser.parse_args([
            "--map", "local/patch.json",
            "--writer", "artifacts/writer.so",
            "--pilot", "artifacts/pilot.so",
            "--state-dir", "private/device-state",
            "--transport-root", "transport",
        ])
        paths = resolve_paths(args, Path("checkout"))
        self.assertEqual(paths.patch_map, Path("local/patch.json"))
        self.assertEqual(paths.writer, Path("artifacts/writer.so"))
        self.assertEqual(paths.pilot, Path("artifacts/pilot.so"))
        self.assertEqual(paths.state_dir, Path("private/device-state"))
        self.assertEqual(paths.transport_root, Path("transport"))

    def test_checkout_defaults_are_repository_relative(self):
        import argparse

        parser = argparse.ArgumentParser()
        add_path_arguments(parser)
        args = parser.parse_args([])
        paths = resolve_paths(args, Path("independent-checkout"))
        self.assertEqual(paths.patch_map, Path("independent-checkout/patch.json"))
        self.assertEqual(paths.writer,
                         Path("independent-checkout/build/pilot_ap_flash.so"))
        self.assertEqual(paths.pilot, Path("independent-checkout/build/pilot.so"))
        self.assertEqual(paths.state_dir,
                         Path("independent-checkout/build/device-state"))
        self.assertEqual(paths.transport_root,
                         Path("fl2_fl3"))

    def test_state_write_does_not_mutate_patch_map(self):
        module = load_on_camera()
        patch_map = {
            "schema": "pilot.patch-map.v1",
            "target": {"model": "测试机", "firmware": "1.0",
                       "binary_sha256": "base", "patched_binary_sha256": "patch"},
            "pilot_dso": {"size_bytes": 12, "sha256": "dso"},
        }
        before = json.dumps(patch_map, ensure_ascii=False, sort_keys=True)
        with tempfile.TemporaryDirectory() as directory:
            module.STATE_DIR = Path(directory)
            module.record_camera_state(patch_map, "write", "before", "after",
                                       b"A6700PILOTFLASH\nRESULT=OK\n")
            saved = Path(directory) / "device-state.json"
            self.assertTrue(saved.is_file())
            state_text = saved.read_text(encoding="utf-8")
            self.assertIn("测试机", state_text)
            self.assertEqual(json.loads(state_text)["camera_applied"], True)
        self.assertEqual(json.dumps(patch_map, ensure_ascii=False, sort_keys=True), before)

    def test_flash_header_follows_the_selected_model_map(self):
        module = load_on_camera()
        self.assertEqual(module.FLASH_HEADER, b"A6700PILOTFLASH\n")
        module.configure_flash_header({"flash": {"header": "A7C2PILOTFLASH\n"}})
        self.assertEqual(module.FLASH_HEADER, b"A7C2PILOTFLASH\n")
        module.configure_flash_header({"schema": "pilot.patch-map.v1"})
        self.assertEqual(module.FLASH_HEADER, b"A6700PILOTFLASH\n")
        with self.assertRaises(RuntimeError):
            module.configure_flash_header({"flash": {"header": "x" * 64}})
        module.FLASH_HEADER = b"A6700PILOTFLASH\n"

    def test_help_does_not_import_transport_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker = root / "imported.txt"
            (root / "cpload_run.py").write_text(
                f"from pathlib import Path\nPath({str(marker)!r}).write_text('imported')\n",
                encoding="utf-8",
            )
            env = os.environ.copy()
            env["PYTHONPATH"] = str(root)
            result = subprocess.run(
                [sys.executable, str(PATCH_DIR / "on_camera.py"), "--help"],
                capture_output=True, text=True, env=env, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("--transport-root", result.stdout)
            self.assertFalse(marker.exists())

    def test_transport_rejects_module_cached_from_another_root(self):
        module = load_on_camera()
        old = sys.modules.get("cpload_run")
        try:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory) / "transport"
                root.mkdir()
                (root / "cpload_run.py").write_text("", encoding="utf-8")
                sys.modules["cpload_run"] = SimpleNamespace(
                    __file__=str(Path(directory) / "other" / "cpload_run.py"))
                with self.assertRaisesRegex(RuntimeError, "outside"):
                    module.load_transport(root)
        finally:
            if old is None:
                sys.modules.pop("cpload_run", None)
            else:
                sys.modules["cpload_run"] = old


if __name__ == "__main__":
    unittest.main()
