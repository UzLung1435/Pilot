"""Model registry integrity: one identity source, no silent drift."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.pilot_tool import models


class ModelRegistryTests(unittest.TestCase):
    def test_c_header_matches_registry(self):
        result = models.verify_c_header()
        self.assertEqual(result["models"], ["a6700", "a7c2"])
        self.assertGreaterEqual(result["checked_definitions"], 26)

    def test_c_header_drift_is_detected(self):
        header = (ROOT / "patch" / "pilot_model.h").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "patch").mkdir()
            (root / "patch" / "pilot_model.h").write_text(
                header.replace('"pilot_a7c2_v1_marker"', '"pilot_a6700_v1_marker"'),
                encoding="utf-8")
            with self.assertRaises(ValueError):
                models.verify_c_header(root)

    def test_runtime_block_carries_foreign_identities(self):
        for model_id in models.model_ids():
            block = models.runtime_block(model_id)
            self.assertEqual(block["model_id"], model_id)
            self.assertEqual(len(block["foreign_model_ids"]), 1)
            self.assertNotIn(model_id, block["foreign_model_ids"])
            self.assertEqual(len(block["foreign_marker_symbols"]), 1)
        self.assertEqual(models.runtime_block("a6700")["dso_path"],
                         "/usr/lib/pilot.so")
        self.assertEqual(models.runtime_block("a7c2")["dso_path"],
                         "/usr/lib/pilot_a7c2.so")

    def test_a7c2_patch_map_runtime_and_island_match_registry(self):
        patch_map = json.loads((ROOT / "patch.a7c2.json").read_text(encoding="utf-8"))
        self.assertEqual(patch_map["runtime"], models.runtime_block("a7c2"))
        island_patch = next(p for p in patch_map["patches"]
                            if p["id"] == "post_fsys_mount_dlopen_island")
        island = bytes.fromhex(island_patch["target_bytes"])
        registry = models.load_models()
        target = registry["models"]["a7c2"]
        for slot, text in target["island_strings"].items():
            offset = int(slot, 16)
            self.assertEqual(island[offset:offset + len(text) + 1],
                             text.encode("ascii") + b"\0", slot)
        for slot, text in registry["models"]["a6700"]["island_strings"].items():
            if text != target["island_strings"][slot]:
                self.assertNotIn(text.encode("ascii"), island, slot)
        self.assertNotIn(b"pilot_a6700", island)

    def test_registry_rejects_oversized_island_string(self):
        data = models.load_models()
        data["models"]["a7c2"]["island_strings"]["0x400"] = "/" + "x" * 40
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(ValueError):
                models.load_models(path)


if __name__ == "__main__":
    unittest.main()
