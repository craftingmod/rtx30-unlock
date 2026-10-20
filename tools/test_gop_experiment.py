import json
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile

from package_gop_experiment import ROOT, package, archive


class GopPackage(unittest.TestCase):
    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'keep.txt'
            marker.write_text('keep')
            with self.assertRaises(ValueError):
                package(directory)
            self.assertEqual(marker.read_text(), 'keep')

    def test_missing_build_creates_no_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'usb'
            with self.assertRaises(OSError):
                package(output, root)
            self.assertFalse(output.exists())

    @unittest.skipUnless((ROOT / 'build/gop-experiment/build-manifest.json').exists(),
                         'Build GOP experiment first')
    def test_package_includes_matching_efi_and_rebuildable_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            output = package(Path(directory) / 'usb')
            self.assertTrue((output / 'RTX30-GOP-TEST').is_dir())
            self.assertTrue((output / 'RTX30-GOP-TEST/README.txt').is_file())
            manifest = json.loads((output / 'build-manifest.json').read_text(encoding='utf-8-sig'))
            self.assertIn(manifest.get('Mode', 'cycle'), ('cycle', 'tree-read-only', 'leaf-member', 'full-flr', 'full-unlock'))
            if manifest.get('Mode') == 'full-unlock':
                self.assertTrue((output / 'RTX3080-UNLOCK/ASSETS/README.txt').is_file())
            if manifest.get('Production'):
                self.assertIn('DEBUG.ON', (output / 'RTX30-GOP-TEST/README.txt').read_text())
                self.assertFalse((output / 'RTX30-GOP-TEST/DEBUG.ON').exists())
            if manifest.get('WindowsHandoff'):
                for source, name in (('experiments/gop/windows-check.ps1', 'check.ps1'),
                                     ('analysis/compute-benchmark.cu', 'compute-benchmark.cu'),
                                     ('analysis/build-benchmark.cmd', 'build-benchmark.cmd'),
                                     ('results/benchmark.json', 'historical-benchmark.json')):
                    self.assertEqual((output / 'WINDOWS' / name).read_bytes(),
                                     (ROOT / source).read_bytes())
                self.assertFalse((output / 'WINDOWS/compute-benchmark.exe').exists())
            self.assertEqual((output / 'EFI/BOOT/BOOTX64.EFI').read_bytes(),
                             (ROOT / 'build/gop-experiment/BOOTX64.EFI').read_bytes())
            for source in ('experiments/gop/cycle.h', 'experiments/gop/cycle.c', 'experiments/gop/tree.c',
                           'experiments/gop/tree.h', 'unlock/runtime.c', 'unlock/runtime.h',
                           'scripts/build-gop-experiment.ps1',
                           'deps/edk2/MdePkg/Include/Uefi.h'):
                self.assertEqual((output / 'SOURCE' / source).read_bytes(),
                                 (ROOT / source).read_bytes())
            self.assertTrue((output / 'LICENSES/EDK2-BSD-2-Clause-Patent.txt').is_file())
            packed = archive(output, Path(directory) / 'usb.zip')
            # Extract the actual distribution; empty directories must not be required.
            with zipfile.ZipFile(packed) as bundle:
                extracted = Path(directory) / 'extracted'
                bundle.extractall(extracted)
                self.assertEqual((extracted / 'EFI/BOOT/BOOTX64.EFI').read_bytes(),
                                 (output / 'EFI/BOOT/BOOTX64.EFI').read_bytes())
                self.assertTrue((extracted / 'RTX30-GOP-TEST/README.txt').is_file())

    def test_zip_refuses_existing_file_and_missing_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            existing = root / 'existing.zip'
            existing.write_bytes(b'keep')
            with self.assertRaises(ValueError):
                archive(root / 'source', existing)
            self.assertEqual(existing.read_bytes(), b'keep')
            source = root / 'source'
            source.mkdir()
            with self.assertRaises(ValueError):
                archive(source, root / 'new.zip')
            self.assertFalse((root / 'new.zip').exists())
            with self.assertRaises(ValueError):
                archive(source, source / 'inside.zip')
            self.assertFalse((source / 'inside.zip').exists())

    @unittest.skipUnless((ROOT / 'build/gop-experiment/build-manifest.json').exists(),
                         'Build GOP experiment first')
    def test_invalid_assets_never_publish_package(self):
        manifest = json.loads((ROOT / 'build/gop-experiment/build-manifest.json').read_text(encoding='utf-8-sig'))
        if manifest.get('Mode') != 'full-unlock':
            self.skipTest('Build full-unlock mode first')
        for invalid in ('missing', 'wrong-size', 'wrong-hash'):
            with self.subTest(invalid=invalid), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                assets = root / 'assets'
                assets.mkdir()
                first = json.loads((ROOT / 'unlock/assets.json').read_text())[0]
                if invalid != 'missing':
                    (assets / first['Name']).write_bytes(b'x' * (first['Bytes'] if invalid == 'wrong-hash' else 1))
                with self.assertRaises((OSError, ValueError)):
                    package(root / 'usb', assets=assets)
                self.assertFalse((root / 'usb').exists())
                self.assertFalse(list(root.glob('.gop-stage-*')))

    @unittest.skipUnless((ROOT / 'build/gop-experiment/build-manifest.json').exists(),
                         'Build GOP experiment first')
    def test_tampering_never_publishes_a_partial_package(self):
        for change in ('efi', 'source', 'escape', 'manifest'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                build = root / 'build/gop-experiment'
                shutil.copytree(ROOT / 'build/gop-experiment', build)
                manifest_path = build / 'build-manifest.json'
                manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
                if change == 'efi':
                    with (build / 'BOOTX64.EFI').open('ab') as stream:
                        stream.write(b'changed')
                elif change == 'manifest':
                    manifest['Sources'] = []
                else:
                    # The first source must fail before any missing later source is read.
                    first = manifest['Sources'][0]
                    if change == 'escape':
                        first['Path'] = '../outside.c'
                    else:
                        path = root / first['Path']
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_text('changed after build')
                manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
                output = root / 'usb'
                with self.assertRaises(ValueError):
                    package(output, root)
                self.assertFalse(output.exists())
                self.assertFalse(list(root.glob('.gop-stage-*')))


if __name__ == '__main__':
    unittest.main()
