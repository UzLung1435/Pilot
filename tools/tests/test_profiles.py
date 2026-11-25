import hashlib
import json
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(REPO / "patch"))

from on_camera_profiles import classify, describe, source_profiles, validate


STOCK = b"AA"
FL23 = b"BB"
STOCK_PATCHED = b"CC"
FL23_PATCHED = b"DD"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_map(accepted=None):
    target = {
        "model": "test body",
        "firmware": "test",
        "binary_size": len(STOCK),
        "binary_sha256": digest(STOCK),
        "patched_binary_sha256": digest(STOCK_PATCHED),
    }
    if accepted is not None:
        target["accepted_sources"] = accepted
    return {"schema": "pilot.patch-map.v1", "target": target, "patches": []}


FL23_ENTRY = {
    "profile": "fl23",
    "binary_sha256": digest(FL23),
    "patched_binary_sha256": digest(FL23_PATCHED),
    "note": "second source",
}


class ProfileTests(unittest.TestCase):
    def test_canonical_map_has_one_source(self):
        rows = source_profiles(make_map())
        self.assertEqual([row["profile"] for row in rows], ["stock"])
        self.assertEqual(rows[0]["binary_sha256"], digest(STOCK))
        self.assertEqual(rows[0]["patched_binary_sha256"], digest(STOCK_PATCHED))

    def test_classify_matches_every_source_and_state(self):
        patch_map = make_map([FL23_ENTRY])
        for data, expected in ((STOCK, ("baseline", "stock")),
                               (STOCK_PATCHED, ("patched", "stock")),
                               (FL23, ("baseline", "fl23")),
                               (FL23_PATCHED, ("patched", "fl23")),
                               (b"ZZ", ("unknown", None))):
            self.assertEqual(classify(data, patch_map), expected, data)

    def test_size_mismatch_is_unknown(self):
        self.assertEqual(classify(b"QQQ", make_map([FL23_ENTRY])),
                         ("unknown", None))

    def test_canonical_entry_is_inserted_first_when_absent(self):
        rows = source_profiles(make_map([FL23_ENTRY]))
        self.assertEqual([row["profile"] for row in rows], ["stock", "fl23"])
        again = source_profiles(make_map([
            {"profile": "stock", "binary_sha256": digest(STOCK),
             "patched_binary_sha256": digest(STOCK_PATCHED)},
            FL23_ENTRY,
        ]))
        self.assertEqual([row["profile"] for row in again], ["stock", "fl23"])

    def test_validate_rejects_bad_and_duplicate_hashes(self):
        validate(make_map([FL23_ENTRY]))
        with self.assertRaises(ValueError):
            validate(make_map([dict(FL23_ENTRY, binary_sha256="not-a-hash")]))
        with self.assertRaises(ValueError):
            validate(make_map([dict(FL23_ENTRY, binary_sha256=digest(STOCK))]))

    def test_describe_names_every_profile(self):
        text = describe(make_map([FL23_ENTRY]))
        self.assertIn("stock=", text)
        self.assertIn("fl23=", text)


class ShippedMapTests(unittest.TestCase):
    def test_a7c2_map_declares_the_fl23_source(self):
        patch_map = json.loads(
            (REPO / "patch.a7c2.json").read_text(encoding="utf-8-sig"))
        validate(patch_map)
        rows = {row["profile"]: row for row in source_profiles(patch_map)}
        self.assertIn("stock", rows)
        self.assertIn("fl23", rows)
        # The canonical stock hashes stay authoritative for the writer build.
        self.assertEqual(rows["stock"]["binary_sha256"],
                         patch_map["target"]["binary_sha256"])
        self.assertEqual(rows["stock"]["patched_binary_sha256"],
                         patch_map["target"]["patched_binary_sha256"])
        for row in rows.values():
            self.assertEqual(row["patched_binary_sha256"],
                             row["patched_binary_sha256"].lower())


if __name__ == "__main__":
    unittest.main()
