import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from probe_profile import generate
from runtime_profile import compile_profile, encode
from test_probe_profile import FAMILIES, PROM, report


class RuntimeProfiles(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'board.json'
        self.output = self.root / 'PROFILE.BIN'
        (self.root / 'PROM.BIN').write_bytes(PROM)

    def fixture(self, identity, chip, mib):
        capture = self.root / 'REPORT.TXT'
        capture.write_text(report(identity, chip=chip, fb_mib=mib), encoding='ascii')
        profile = generate(capture, self.root / 'PROM.BIN')
        self.source.write_text(json.dumps(profile), encoding='utf-8')
        return profile

    def test_five_ga10x_families_and_wire_layout(self):
        for identity, chip, mib in FAMILIES.values():
            profile = self.fixture(identity, chip, mib)
            data = encode(profile)
            self.assertEqual(len(data), 176)
            self.assertEqual(struct.unpack_from('<8s4I', data),
                             (b'GUPROF17', 1, 176, identity, 0x12347377))
            self.assertEqual(struct.unpack_from('<I', data, 24)[0], chip)
            self.assertEqual(struct.unpack_from('<I', data, 44)[0], mib)
            self.assertEqual(struct.unpack_from('<Q', data, 48)[0], 0x1000000)
            self.assertEqual(data[80:112], hashlib.sha256(PROM).digest())
            self.assertEqual(data[144:], hashlib.sha256(data[:144]).digest())

    def test_checked_compile_and_exclusive_create(self):
        profile = self.fixture(*FAMILIES['GA102'])
        self.assertEqual(compile_profile(self.source, self.output), encode(profile))
        with self.assertRaises(FileExistsError):
            compile_profile(self.source, self.output)

    def test_edited_evidence_is_refused_without_output(self):
        profile = self.fixture(*FAMILIES['GA102'])
        profile['hardware']['promSha256'] = '0' * 64
        self.source.write_text(json.dumps(profile), encoding='utf-8')
        with self.assertRaises(ValueError):
            compile_profile(self.source, self.output)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
