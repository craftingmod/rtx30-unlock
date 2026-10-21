import copy
import json
from pathlib import Path
import tempfile
import unittest

from check_profile import check
from probe_profile import generate, parse, REGISTERS, sha

ROOT = Path(__file__).resolve().parents[1]
PROM = b'\x55\xaa\0\0' + b'\x11\x22\x33\x44' * (1048576 // 4 - 1)
FAMILIES = {
    'GA102': (0x220310de, 0xb72000a1, 24576),
    'GA103': (0x241410de, 0xb73000a1, 8192),
    'GA104': (0x248410de, 0xb74000a1, 8192),
    'GA106': (0x250410de, 0xb76000a1, 12288),
    'GA107': (0x25a210de, 0xb77000a1, 4096),
}


def h(key, value):
    return f'{key}=0x{value:016x}\n'


def report(identity=0x220310de, subsystem=0x12347377, complete=True,
           chip=0xb72000a1, fb_mib=24576):
    config = [0] * 64
    config[0], config[1], config[2], config[11] = identity, 2, 0x030000a1, subsystem
    text = '[REPORT]\n' + h('format', 1) + '[OUTPUT]\n' + h('pci_enumeration_status', 0)
    text += '[PCI:0000]\n'
    for key, value in dict(location_status=0, segment=0, bus=9, device=0, function=0,
                           identity=identity, class_revision=config[2], subsystem=subsystem,
                           detail_supported=1).items():
        text += h(key, value)
    text += ''.join(h(f'config_{i * 4:02x}', value) for i, value in enumerate(config))
    for bar in range(6):
        text += f'[BAR:{bar:04x}]\n' + h('pci_index', 0) + h('bar_index', bar)
        text += h('status', 0x8000000000000003)
    text += '[SUMMARY]\n' + h('matching_cards', 1) + h('gop_enumeration_status', 0x800000000000000e)
    text += '[DETAIL]\n' + h('inventory_save_status', 0)
    text += '[TARGET]\n' + h('pci_index', 0) + h('collect_status', 0 if complete else 0x8000000000000003)
    if complete:
        for key, value in dict(identity=identity, subsystem=subsystem, bar0_status=0,
                               bar0_base=0x84000000, bar0_length=0x1000000, memory_decode_enabled=1).items():
            text += h(key, value)
        for i, offset in enumerate(REGISTERS):
            text += f'[REG:{i:04x}]\n' + h('register', offset) + h('status', 0)
            values = {0: chip, 0x118128: 1, 0x118234: 0xff, 0x110100: 0x10,
                      0x1183a4: fb_mib, 0x1fa7c8: 1, 0x823804: 0xffffff8f,
                      0x82381c: 0x46254040, 0x823820: 2}
            text += h('value', values.get(offset, 0))
        text += '[DUMPS]\nprom_sha256=' + sha(PROM) + '\n'
        text += h('prom_size', len(PROM)) + h('prom_read_status', 0) + h('prom_save_status', 0)
    return text + '[END]\ngpu_writes=none\ngpu_resets=none\nboot_variable_changes=none\n'


class Profiles(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.report = self.root / 'REPORT.TXT'
        self.prom = self.root / 'PROM.BIN'
        self.report.write_text(report(), encoding='ascii')
        self.prom.write_bytes(PROM)

    def test_ga102_candidate_never_auto_approved(self):
        profile = generate(self.report, self.prom)
        self.assertEqual(profile['target']['deviceId'], '2203')
        self.assertEqual(profile['target']['subsystemVendorId'], '7377')
        self.assertEqual(profile['target']['pciAddress'], '0000:09:00.0')
        self.assertTrue(profile['validation']['probeComplete'])
        self.assertFalse(profile['target']['architectureVerified'])
        self.assertFalse(profile['validation']['unlockApproved'])
        self.assertFalse(profile['validation']['firmwareCompatible'])
        self.assertIsNone(profile['hardware']['vramSize'])
        self.assertEqual(profile['inputs']['prom']['sha256'], sha(PROM))

    def test_unsupported_inventory_produces_incomplete_profile(self):
        self.report.write_text(report(0x268410de, complete=False), encoding='ascii')
        profile = generate(self.report)
        self.assertEqual(profile['target']['architecture'], 'unknown')
        self.assertFalse(profile['validation']['probeComplete'])

    def test_malformed_reports(self):
        good = report()
        cases = [good.replace('[REPORT]', '[UNKNOWN]'), good.replace(h('format', 1), ''),
                 good.replace(h('format', 1), h('format', 1) * 2),
                 good.replace('[TARGET]', '[TARGET]\n[TARGET]'),
                 good.replace(h('bus', 9), h('bus', 256)),
                 good.replace(h('matching_cards', 1), h('matching_cards', 2)),
                 good.replace('[END]', '[SKIPPED]'), good.replace('[BAR:0005]', '[BAR:0006]'),
                 good.replace('config_00=', 'config_04='),
                 good.replace('gpu_writes=none', 'gpu_writes=yes'),
                 good.replace(h('format', 1), h('format', 2)),
                 good.replace(h('bus', 9), 'bus=-1\n'), good + '[END]\n',
                 good.replace('[REG:000f]', '[REG:0010]'),
                 good.replace(h('format', 1), h('format', 1) + 'typo=1\n')]
        for bad in cases:
            with self.subTest(bad=bad[:60]), self.assertRaises(ValueError):
                parse(bad.encode('ascii'))

    def test_corrupt_or_unrelated_prom(self):
        for data in (b'', PROM[:-1], b'bad!' + PROM[4:], PROM[:8] + b'changed!' + PROM[16:]):
            self.prom.write_bytes(data)
            with self.assertRaises(ValueError):
                generate(self.report, self.prom)

    def test_inaccessible_samples_are_incomplete(self):
        self.report.write_text(report().replace(h('value', 0xb72000a1), h('value', 0xffffffff)), encoding='ascii')
        self.assertFalse(generate(self.report, self.prom)['validation']['probeComplete'])

    def test_unlock_gate_rejects_new_boards_and_modified_evidence(self):
        profile = generate(self.report, self.prom)
        path = self.root / 'board.json'
        path.write_text(json.dumps(profile), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'approval'):
            check(path)
        approved = copy.deepcopy(profile)
        approved['validation'] = {key: True for key in profile['validation']}
        approved['target']['architectureVerified'] = True
        approved['hardware']['vramSize'] = 10737418240
        approved['preboot']['displayRoute'] = 'intel-uhd770'
        path.write_text(json.dumps(approved), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'no validated'):
            check(path)
        self.prom.write_bytes(b'\x55\xaa' + b'\0' * (1048576 - 2))
        with self.assertRaisesRegex(ValueError, 'PROM'):
            check(path)

    def test_duplicate_json_keys_rejected(self):
        path = self.root / 'board.json'
        path.write_text('{"schema": 1, "schema": 1}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            check(path)

    def test_experimental_ga102_uses_capture_without_approval(self):
        from check_profile import header
        profile = generate(self.report, self.prom)
        path = self.root / 'board.json'
        path.write_text(json.dumps(profile), encoding='utf-8')
        result = check(path, experimental=True)
        generated = header(result)
        self.assertIn('#define GU_BOARD_ID 0x220310deU', generated)
        self.assertIn('#define GU_FB_MIB 0x00006000U', generated)
        self.assertIn('#define GU_BOARD_SUBSYSTEM 0x12347377U', generated)
        self.assertFalse(result['validation']['unlockApproved'])
        for offset, value in [(0, 0xb92000a1), (0x1fa828, 0x100), (0x1183a4, 0), (0x110100, 0)]:
            text = report()
            lines = text.splitlines()
            index = lines.index(f'[REG:{REGISTERS.index(offset):04x}]')
            lines[index + 3] = h('value', value).strip()
            self.report.write_text('\n'.join(lines) + '\n', encoding='ascii')
            changed = generate(self.report, self.prom)
            path.write_text(json.dumps(changed), encoding='utf-8')
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                check(path, experimental=True)

    def test_all_ga10x_families_use_measured_chip_and_remain_experimental(self):
        from check_profile import header
        path = self.root / 'board.json'
        for family, (identity, chip, mib) in FAMILIES.items():
            with self.subTest(family=family):
                self.report.write_text(report(identity, chip=chip, fb_mib=mib), encoding='ascii')
                profile = generate(self.report, self.prom)
                self.assertEqual(profile['target']['architecture'], family)
                self.assertTrue(profile['validation']['probeComplete'])
                self.assertFalse(profile['target']['architectureVerified'])
                self.assertFalse(profile['validation']['firmwareCompatible'])
                self.assertFalse(profile['validation']['unlockApproved'])
                path.write_text(json.dumps(profile), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'approval'):
                    check(path)
                accepted = check(path, experimental=True)
                generated = header(accepted)
                self.assertIn(f'#define GU_BOARD_CHIP 0x{chip:08x}U', generated)
                self.assertIn(f'#define GU_BOARD_ID 0x{identity:08x}U', generated)
                self.assertIn(f'#define GU_FB_MIB 0x{mib:08x}U', generated)

    def test_non_ga10x_chips_and_non_candidate_pci_are_refused(self):
        path = self.root / 'board.json'
        for identity, chip in [(0x20b010de, 0xb70000a1), (0x268410de, 0xb92000a1),
                               (0x250410de, 0xb70000a1), (0x250410de, 0xb62000a1),
                               (0x250410de, 0xb75000a1), (0x268410de, 0xb76000a1)]:
            with self.subTest(identity=identity, chip=chip):
                self.report.write_text(report(identity, chip=chip), encoding='ascii')
                profile = generate(self.report, self.prom)
                path.write_text(json.dumps(profile), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'GA10x'):
                    check(path, experimental=True)

    @unittest.skipUnless((ROOT / 'build/probe-test-report.txt').exists(), 'Run bun tools/test.ts probe for emitted-report integration')
    def test_actual_c_report_format(self):
        captured = ROOT / 'build/probe-test-report.txt'
        profile = generate(captured, self.prom)
        self.assertEqual(profile['target']['deviceId'], '2206')
        self.assertTrue(profile['validation']['probeComplete'])


if __name__ == '__main__':
    import sys
    if len(sys.argv) in (3, 4) and sys.argv[1] == '--write-fixture':
        from check_profile import header
        root = Path(sys.argv[2]).resolve()
        root.mkdir(parents=True, exist_ok=True)
        identity, chip, mib = FAMILIES[sys.argv[3] if len(sys.argv) == 4 else 'GA102']
        (root / 'REPORT.TXT').write_text(report(identity, chip=chip, fb_mib=mib), encoding='ascii')
        (root / 'PROM.BIN').write_bytes(PROM)
        profile = generate(root / 'REPORT.TXT', root / 'PROM.BIN')
        (root / 'board.json').write_text(json.dumps(profile, indent=2), encoding='utf-8')
        check(root / 'board.json', experimental=True)
        (root / 'profile.h').write_text(header(profile), encoding='ascii')
    else:
        unittest.main()
