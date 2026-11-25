import hashlib
import json
import tempfile
import unittest
from unittest import mock
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pilot_tool.deploy import deploy_module, export_logs, list_modules


def elf64_aarch64(payload=b"module"):
    header = bytearray(64)
    header[:4] = b"\x7fELF"
    header[4:6] = b"\x02\x01"  # ELF64, little endian
    header[6] = 1
    header[18:20] = (183).to_bytes(2, "little")
    return bytes(header) + payload


class DeployTests(unittest.TestCase):
    def test_update_keeps_load_order_and_rejects_card_side_backup(self):
        pilot = self.card / "pilot"
        pilot.mkdir()
        manifest = pilot / "modules.txt"
        manifest.write_text('{"modules": ["demo.so", "later.so"]}', encoding="utf-8")
        with self.assertRaises(ValueError):
            deploy_module(self.source, self.card, self.digest(), pilot / "backups")
        self.assertFalse((pilot / "backups").exists())
        result = deploy_module(self.source, self.card, self.digest(), self.backups)
        self.assertEqual(result["modules"], ["demo.so", "later.so"])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.card = self.root / "card"
        self.card.mkdir()
        self.source = self.root / "demo.so"
        self.source.write_bytes(elf64_aarch64())
        self.backups = self.root / "backups"

    def tearDown(self):
        self.temp.cleanup()

    def digest(self, path=None):
        return hashlib.sha256((path or self.source).read_bytes()).hexdigest()

    def test_deploy_and_list_hash(self):
        result = deploy_module(self.source, self.card, self.digest(), self.backups)
        installed = self.card / "pilot" / "demo.so"
        self.assertEqual(result["sha256"], self.digest())
        self.assertEqual(result["modules"], ["demo.so"])
        self.assertEqual(list_modules(self.card), [{"name": "demo.so", "sha256": self.digest(installed)}])

    def test_bad_hash_and_non_elf_do_not_change_card(self):
        with self.assertRaises(ValueError):
            deploy_module(self.source, self.card, "0" * 64, self.backups)
        self.source.write_bytes(b"not an ELF")
        with self.assertRaises(ValueError):
            deploy_module(self.source, self.card, self.digest(), self.backups)
        self.assertFalse((self.card / "pilot").exists())

    def test_rejects_unknown_fields_and_bad_manifest(self):
        pilot = self.card / "pilot"
        pilot.mkdir()
        manifest = pilot / "modules.txt"
        for raw in ('{"modules": [], "extra": 1}', '{"modules": "demo.so"}', '{"modules": ["../bad.so"]}'):
            manifest.write_text(raw, encoding="utf-8")
            with self.assertRaises(ValueError):
                deploy_module(self.source, self.card, self.digest(), self.backups)
        self.assertFalse((pilot / "demo.so").exists())

    def test_backups_keep_existing_manifest_and_same_name_module(self):
        pilot = self.card / "pilot"
        pilot.mkdir()
        manifest = pilot / "modules.txt"
        manifest.write_text('{"modules": ["old.so"]}', encoding="utf-8")
        old = pilot / "demo.so"
        old.write_bytes(b"old module")
        result = deploy_module(self.source, self.card, self.digest(), self.backups)
        backup = Path(result["backup_dir"])
        self.assertEqual((backup / "modules.txt").read_text(encoding="utf-8"), '{"modules": ["old.so"]}')
        self.assertEqual((backup / "demo.so").read_bytes(), b"old module")
        self.assertEqual(json.loads(manifest.read_text(encoding="utf-8")), {"modules": ["old.so", "demo.so"]})

    def test_model_guard_accepts_matching_tag(self):
        self.source.write_bytes(elf64_aarch64(b"pilot_module_model_a7c2\x00"))
        result = deploy_module(self.source, self.card, self.digest(),
                               self.backups, model="a7c2")
        self.assertEqual(result["model"], "a7c2")
        self.assertEqual(result["model_tag"], "pilot_module_model_a7c2")
        self.assertTrue((self.card / "pilot" / "demo.so").is_file())

    def test_model_guard_rejects_foreign_or_missing_tag(self):
        for payload in (b"pilot_module_model_a6700\x00", b"plain module"):
            self.source.write_bytes(elf64_aarch64(payload))
            with self.assertRaisesRegex(ValueError, "not built for a7c2"):
                deploy_module(self.source, self.card, self.digest(),
                              self.backups, model="a7c2")
        self.assertFalse((self.card / "pilot").exists())

    def test_model_guard_rejects_foreign_filename_and_unknown_model(self):
        foreign = self.root / "a6700_demo.so"
        foreign.write_bytes(elf64_aarch64(b"pilot_module_model_a7c2\x00"))
        with self.assertRaisesRegex(ValueError, "tagged for a6700"):
            deploy_module(foreign, self.card, self.digest(foreign),
                          self.backups, model="a7c2")
        with self.assertRaisesRegex(ValueError, "unknown model"):
            deploy_module(self.source, self.card, self.digest(),
                          self.backups, model="unknown-body")

    def test_export_rejects_symlink_without_reading_target(self):
        pilot = self.card / "pilot"
        module_dir = pilot / "modules" / "demo"
        module_dir.mkdir(parents=True)
        (pilot / "modules.txt").write_text('{"modules": ["demo.so"]}', encoding="utf-8")
        outside = self.root / "private.log"
        outside.write_text("private", encoding="utf-8")
        log_path = module_dir / "module.log"
        # Simulate a symlink for Windows environments where creating one may
        # require elevated privileges. The exporter must reject it before open.
        original = Path.is_symlink

        def is_symlink(path):
            return path == log_path or original(path)

        with mock.patch.object(Path, "is_symlink", is_symlink):
            with self.assertRaises(ValueError):
                export_logs(self.card, self.root / "export")
        self.assertFalse((self.root / "export" / "modules" / "demo" / "module.log").exists())

    def test_legacy_json_manifest_is_read_and_migrated(self):
        pilot = self.card / "pilot"
        pilot.mkdir()
        legacy = pilot / "modules.json"
        legacy.write_text('{"modules": ["old.so"]}', encoding="utf-8")
        result = deploy_module(self.source, self.card, self.digest(), self.backups)
        self.assertEqual(result["modules"], ["old.so", "demo.so"])
        migrated = json.loads((pilot / "modules.txt").read_text(encoding="utf-8"))
        self.assertEqual(migrated, {"modules": ["old.so", "demo.so"]})
        # The legacy file keeps the same list so older tooling stays consistent.
        self.assertEqual(json.loads(legacy.read_text(encoding="utf-8")), migrated)


if __name__ == "__main__":
    unittest.main()
