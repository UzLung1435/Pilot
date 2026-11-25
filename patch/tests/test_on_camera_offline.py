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
REPO_ROOT = PATCH_DIR.parent
sys.path.insert(0, str(PATCH_DIR))
sys.path.insert(0, str(REPO_ROOT))
from on_camera_config import add_path_arguments, resolve_paths
from tools.pilot_tool import models as pilot_models


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
        self.assertEqual(paths.patch_map, Path("independent-checkout/patch.a6700.json"))
        self.assertEqual(paths.writer,
                         Path("independent-checkout/patch/pilot_ap_flash.so"))
        self.assertEqual(paths.pilot, Path("independent-checkout/build/pilot.so"))
        self.assertEqual(paths.state_dir,
                         Path("independent-checkout/build/device-state"))
        self.assertEqual(paths.transport_root,
                         Path("independent-checkout/patch"))

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

    def _a7c2_map(self):
        # Build the map from the registry so this public test does not depend
        # on research-only files such as patch.a7c2.json.
        return {"schema": "pilot.patch-map.v1",
                "runtime": pilot_models.runtime_block("a7c2")}

    def test_model_runtime_switches_device_path_and_writer_header(self):
        module = load_on_camera()
        runtime = module.configure_model(self._a7c2_map())
        self.assertEqual(runtime["model_id"], "a7c2")
        self.assertEqual(module.PILOT_DEVICE_PATH,
                         "/tmp/jiritsu/../../usr/lib/pilot_a7c2.so")
        self.assertEqual(module.PILOT_DSO_DISPLAY_PATH, "/usr/lib/pilot_a7c2.so")
        self.assertEqual(module.MODEL_FLASH_HEADER, b"A7C2PILOTFLASH\n")
        module.configure_flash_header(self._a7c2_map())
        self.assertEqual(module.FLASH_HEADER, b"A7C2PILOTFLASH\n")

        legacy = module.configure_model({"schema": "pilot.patch-map.v1"})
        self.assertEqual(legacy["model_id"], "a6700")
        self.assertEqual(module.PILOT_DEVICE_PATH,
                         "/tmp/jiritsu/../../usr/lib/pilot.so")
        module.configure_flash_header({})
        self.assertEqual(module.FLASH_HEADER, b"A6700PILOTFLASH\n")

    def test_legacy_runtime_matches_the_registry(self):
        module = load_on_camera()
        self.assertEqual(dict(module.LEGACY_RUNTIME),
                         pilot_models.runtime_block("a6700"))
        self.assertEqual(sorted(pilot_models.model_ids()), ["a6700", "a7c2"])

    def test_pilot_dso_model_guard(self):
        module = load_on_camera()
        patch_map = self._a7c2_map()
        good = (b"\x7fELF" + b"\0" * 8 +
                b"\0".join((b"pilot_a7c2_v1_marker",
                            b"pilot_a7c2_v1_on_mount",
                            b"pilot_a7c2_get_api")) + b"\0")
        module.validate_pilot_dso(good, patch_map)
        with self.assertRaisesRegex(RuntimeError, "foreign marker"):
            module.validate_pilot_dso(good + b"pilot_a6700_v1_marker\0",
                                      patch_map)
        with self.assertRaisesRegex(RuntimeError, "not a a7c2 build"):
            module.validate_pilot_dso(b"\x7fELF nothing here\0", patch_map)

    def test_runtime_rejects_mixed_model_identity(self):
        module = load_on_camera()
        patch_map = self._a7c2_map()
        patch_map["runtime"]["marker_symbol"] = "pilot_a6700_v1_marker"
        with self.assertRaises(RuntimeError):
            module.resolve_runtime(patch_map)
        patch_map = self._a7c2_map()
        del patch_map["runtime"]["foreign_marker_symbols"]
        with self.assertRaises(RuntimeError):
            module.resolve_runtime(patch_map)

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

    def test_generic_writer_stages_plan_and_pilot(self):
        module = load_on_camera()
        from prepare_ap import WINDOW_IDS, plan_from_config
        class Session:
            def __init__(self):
                self.files = {}
                self.path = None
                self.staged = {}
            def select(self, path):
                self.path = path
            def read_path(self, path, missing=False):
                return self.files.get(path)
            def jiritsu(self, command, data, missing=False):
                if command == 18:
                    self.files.pop(self.path, None)
                elif command == 16:
                    self.files[self.path] = data[3:]
                    self.staged[self.path] = data[3:]
                    if self.path == module.FLASH_FILES["trigger"]:
                        self.files[module.FLASH_FILES["result"]] = b"A6700PILOTFLASH\nRESULT=OK\n"
        patch_map = {"target": {"binary_size": 256}, "patches": [
            {"id": name, "file_offset": offset, "length": 4,
             "original_bytes": "00000000", "target_bytes": "01020304"}
            for name, offset in zip(WINDOW_IDS, (32, 64, 96, 128))]}
        session = Session()
        result = module.run_flash_module(session, b"writer", "write", patch_map, b"pilot")
        self.assertIn(b"RESULT=OK", result)
        self.assertEqual(session.staged[module.FLASH_FILES["plan"]], plan_from_config(patch_map))
        self.assertEqual(session.staged[module.FLASH_FILES["pilot"]], b"pilot")
        self.assertNotIn(module.FLASH_FILES["plan"], session.files)
        self.assertNotIn(module.FLASH_FILES["pilot"], session.files)

    def test_ap_state_ignores_non_window_bytes_and_whole_file_hashes(self):
        module = load_on_camera()
        from prepare_ap import WINDOW_IDS
        patch_map = {
            "schema": "pilot.patch-map.v1",
            "target": {"model": "test", "firmware": "2.00", "binary_size": 256},
            "patches": [
                {"id": name, "file_offset": hex(offset), "length": 4,
                 "original_bytes": "00000000", "target_bytes": "01020304"}
                for name, offset in zip(WINDOW_IDS, (32, 64, 96, 128))
            ],
        }
        image = bytearray(256)
        image[0:16] = b"MODIFIED_OUTSIDE"
        self.assertEqual(module.ap_state(bytes(image), patch_map), "baseline")
        for offset in (32, 64, 96, 128):
            image[offset:offset + 4] = bytes.fromhex("01020304")
        self.assertEqual(module.ap_state(bytes(image), patch_map), "patched")
        image[32:36] = b"DIFF"
        self.assertEqual(module.ap_state(bytes(image), patch_map), "unknown")

    def test_load_writer_and_pilot_do_not_require_optional_metadata(self):
        module = load_on_camera()
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            writer_path = tmp / "pilot_ap_flash.so"
            pilot_path = tmp / "pilot.so"
            writer = bytearray(128)
            writer[:6] = b"\x7fELF\x02\x01"
            writer[18:20] = (183).to_bytes(2, "little")
            writer[64:84] = b"pilot_ap_generic_v1\0"
            writer_path.write_bytes(writer)
            pilot_path.write_bytes(b"pilot-dso")
            module.FLASH_MODULE_PATH = writer_path
            module.PILOT_DSO_PATH = pilot_path
            self.assertEqual(module.load_writer({}), bytes(writer))
            self.assertEqual(module.load_pilot({}), b"pilot-dso")

    def test_force_write_allows_unknown_windows_and_stages_actual_before_bytes(self):
        from unittest.mock import patch
        module = load_on_camera()
        from prepare_ap import WINDOW_IDS, plan_from_config
        patch_map = {
            "schema": "pilot.patch-map.v1",
            "target": {"model": "test", "firmware": "2.00", "binary_size": 256},
            "patches": [
                {"id": name, "file_offset": hex(offset), "length": 4,
                 "original_bytes": "00000000", "target_bytes": "01020304"}
                for name, offset in zip(WINDOW_IDS, (32, 64, 96, 128))
            ],
        }
        before = bytearray(256)
        before[32:36] = b"DIFF"
        after = bytearray(before)
        for offset in (32, 64, 96, 128):
            after[offset:offset + 4] = bytes.fromhex("01020304")
        session_files = {"/tmp/jiritsu/../../usr/lib/pilot.so": None}
        staged = {}

        class Session:
            def __init__(self):
                self.path = None
                self.ap_reads = 0
            def select(self, path):
                self.path = path
            def read_path(self, path, missing=False):
                if path == module.AP_PATH:
                    self.ap_reads += 1
                    return bytes(before) if self.ap_reads == 1 else bytes(after)
                return session_files.get(path)
            def jiritsu(self, command, data, missing=False):
                if command == 18:
                    session_files.pop(self.path, None)
                elif command == 16:
                    session_files[self.path] = data[3:]
                    staged[self.path] = data[3:]
                    if self.path == module.FLASH_FILES["trigger"]:
                        session_files[module.FLASH_FILES["result"]] = b"A6700PILOTFLASH\nRESULT=OK\n"
                        session_files[module.PILOT_DEVICE_PATH] = b"pilot"

        with tempfile.TemporaryDirectory() as directory:
            module.STATE_DIR = Path(directory)
            with self.assertRaisesRegex(RuntimeError, "--force"):
                module.run_write(Session(), patch_map, b"writer", b"pilot", "write", force=False)
            session = Session()
            with patch.object(module, "remove_result_if_matches"):
                rc = module.run_write(session, patch_map, b"writer", b"pilot", "write", force=True)
            self.assertEqual(rc, 0)
            expected_map = module.with_actual_windows(patch_map, bytes(before), "write")
            self.assertEqual(expected_map["patches"][0]["original_bytes"], b"DIFF".hex())
            self.assertEqual(staged[module.FLASH_FILES["plan"]], plan_from_config(expected_map))


if __name__ == "__main__":
    unittest.main()
