"""Test output layout, input verification and refusal to overwrite existing data."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

SCRIPT = Path(__file__).with_name('package_usb.py')
PROJECT = Path(__file__).resolve().parents[1]


class UsbPackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.root = self.base / 'source'
        self.assets = self.base / 'assets'
        self.output = self.base / 'usb'
        self.assets.mkdir()
        (self.root / 'unlock/build').mkdir(parents=True)
        (self.root / 'docs').mkdir()
        (self.root / 'LICENSES').mkdir()
        (self.root / 'LICENSE').write_text('GPL test notice\n')
        (self.root / 'LICENSES/EDK2.txt').write_text('EDK2 test notice\n')
        (self.root / 'docs/USB-BOOT.md').write_text('Test USB instructions\n')
        (self.root / 'unlock/app.c').write_bytes(b'/* source fixture */\n')
        (self.root / 'unlock/verify_pe.py').write_bytes((PROJECT / 'unlock/verify_pe.py').read_bytes())
        # Minimal PE accepted by the real verifier, with an executable EFI entry.
        data = bytearray(1024)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x80)
        data[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<HH', data, 0x84, 0x8664, 1)
        struct.pack_into('<H', data, 0x94, 240)
        optional = 0x98
        struct.pack_into('<H', data, optional, 0x20b)
        struct.pack_into('<I', data, optional + 16, 0x1000)
        struct.pack_into('<HH', data, optional + 68, 10, 0x140)
        struct.pack_into('<I', data, optional + 108, 16)
        struct.pack_into('<II', data, optional + 112 + 8 * 5, 0x2000, 12)
        section = optional + 240
        struct.pack_into('<II', data, section + 8, 128, 0x1000)
        struct.pack_into('<I', data, section + 36, 0x20000000)
        self.efi = self.root / 'unlock/build/BOOTX64.EFI'
        self.efi.write_bytes(data)
        (self.efi.parent / 'BOOTX64.map').write_text(' 0001:00000000 efi_main 0000000000001000\n')
        (self.efi.parent / 'build-manifest.json').write_text(json.dumps({
            'EfiSha256': hashlib.sha256(data).hexdigest(),
            'Sources': [{'Path': 'app.c', 'Sha256': hashlib.sha256((self.root / 'unlock/app.c').read_bytes()).hexdigest()}],
        }))
        specs = []
        for n in range(5):
            payload = ('asset-' + str(n)).encode()
            name = 'asset' + str(n) + '.bin'
            (self.assets / name).write_bytes(payload)
            specs.append({'Name': name, 'Bytes': len(payload), 'Sha256': hashlib.sha256(payload).hexdigest()})
        (self.root / 'unlock/assets.json').write_text(json.dumps(specs))
        self.refresh_manifest()
        path = self.efi.parent / 'build-manifest.json'
        data = json.loads(path.read_text())
        data['SourceManifestSha256'] = hashlib.sha256((self.root / 'SOURCE-MANIFEST.json').read_bytes()).hexdigest()
        path.write_text(json.dumps(data))

    def refresh_manifest(self):
        paths = [p for p in self.root.rglob('*') if p.is_file() and 'build' not in p.relative_to(self.root).parts and p.name != 'SOURCE-MANIFEST.json']
        (self.root / 'SOURCE-MANIFEST.json').write_text(json.dumps({
            'schema': 1, 'files': [{'path': p.relative_to(self.root).as_posix(), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]
        }))

    def run_package(self, with_assets=True):
        args = [sys.executable, str(SCRIPT), '--project', str(self.root), '--output', str(self.output)]
        if with_assets:
            args += ['--assets', str(self.assets)]
        return subprocess.run(args, capture_output=True, text=True)

    def test_complete_layout_checksums_source_and_notices(self):
        (self.root / '.env').write_text('PRIVATE_LOCAL_DATA=example\n')
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.output / 'EFI/BOOT/BOOTX64.EFI').read_bytes(), self.efi.read_bytes())
        self.assertEqual((self.output / 'RTX-UNLOCK/ASSETS/asset2.bin').read_bytes(), b'asset-2')
        self.assertTrue((self.output / 'RTX-UNLOCK/LICENSES/GPL-2.0.txt').is_file())
        self.assertTrue((self.output / 'RTX-UNLOCK/LICENSES/EDK2.txt').is_file())
        with zipfile.ZipFile(self.output / 'RTX-UNLOCK/SOURCE.zip') as archive:
            self.assertEqual(archive.read('unlock/app.c'), b'/* source fixture */\n')
            self.assertNotIn('.env', archive.namelist())
            self.assertNotIn('unlock/build/BOOTX64.EFI', archive.namelist())
        hashes = (self.output / 'SHA256SUMS.txt').read_text()
        self.assertIn(hashlib.sha256(b'asset-2').hexdigest() + '  RTX-UNLOCK/ASSETS/asset2.bin', hashes)

    def test_missing_assets_produces_explicit_incomplete_template(self):
        result = self.run_package(with_assets=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((self.output / 'PACKAGE.json').read_text())
        self.assertFalse(manifest['assets_included'])
        self.assertFalse(manifest['ready_for_boot'])
        self.assertTrue((self.output / 'RTX-UNLOCK/ASSETS/REQUIRED-ASSETS.json').is_file())

    def test_changed_asset_is_rejected_before_output_creation(self):
        (self.assets / 'asset1.bin').write_bytes(b'changed')
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('asset', result.stderr.lower())
        self.assertFalse(self.output.exists())

    def test_changed_efi_is_rejected_before_output_creation(self):
        with self.efi.open('ab') as stream:
            stream.write(b'changed')
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('EFI', result.stderr)
        self.assertFalse(self.output.exists())

    def test_changed_source_is_rejected_before_output_creation(self):
        (self.root / 'unlock/app.c').write_text('/* unverified edit */\n')
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source', result.stderr.lower())
        self.assertFalse(self.output.exists())

    def test_existing_directory_is_never_overwritten(self):
        self.output.mkdir()
        sentinel = self.output / 'my-file.txt'
        sentinel.write_text('keep me')
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('already exists', result.stderr)
        self.assertEqual(sentinel.read_text(), 'keep me')
        self.assertEqual(list(self.output.iterdir()), [sentinel])

    def test_refreshed_source_manifest_requires_rebuild(self):
        (self.root / 'LICENSE').write_text('Updated notice\n')
        self.refresh_manifest()
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('rebuild', result.stderr.lower())
        self.assertFalse(self.output.exists())

    def test_efi_change_during_copy_is_rejected(self):
        spec = importlib.util.spec_from_file_location('package_under_test', SCRIPT)
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        copyfile = package.shutil.copyfile

        def concurrent_rebuild(source, destination):
            result = copyfile(source, destination)
            if Path(source).resolve() == self.efi.resolve():
                Path(destination).write_bytes(b'changed during copy')
            return result

        with mock.patch.object(package.shutil, 'copyfile', side_effect=concurrent_rebuild):
            with self.assertRaisesRegex(ValueError, 'EFI'):
                package.prepare(self.root, self.assets, self.output)
        self.assertFalse(self.output.exists())

    def test_source_change_after_validation_is_rejected(self):
        spec = importlib.util.spec_from_file_location('package_under_test', SCRIPT)
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        copyfile = package.shutil.copyfile

        def concurrent_edit(source, destination):
            result = copyfile(source, destination)
            if Path(source).resolve() == self.efi.resolve():
                (self.root / 'unlock/app.c').write_text('/* concurrent edit */\n')
            return result

        with mock.patch.object(package.shutil, 'copyfile', side_effect=concurrent_edit):
            with self.assertRaisesRegex(ValueError, 'source'):
                package.prepare(self.root, self.assets, self.output)
        self.assertFalse(self.output.exists())

    def test_source_manifest_cannot_escape_project(self):
        p = self.root / 'SOURCE-MANIFEST.json'
        data = json.loads(p.read_text())
        data['files'].append({'path': '../outside.txt', 'sha256': '0' * 64})
        p.write_text(json.dumps(data))
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source path', result.stderr)
        self.assertFalse(self.output.exists())

    def test_malformed_efi_is_rejected_even_when_hash_matches(self):
        raw = bytearray(self.efi.read_bytes())
        struct.pack_into('<H', raw, 0x98 + 68, 3)
        self.efi.write_bytes(raw)
        p = self.efi.parent / 'build-manifest.json'
        data = json.loads(p.read_text())
        data['EfiSha256'] = hashlib.sha256(raw).hexdigest()
        p.write_text(json.dumps(data))
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('EFI application', result.stderr)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
