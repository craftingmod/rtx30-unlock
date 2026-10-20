from pathlib import Path
import tempfile
import unittest
from package_probe_usb import package, ROOT


class ProbePackage(unittest.TestCase):
    def test_existing_output_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                package(directory)

    def test_missing_build_leaves_no_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'usb'
            with self.assertRaises(OSError):
                package(output, root)
            self.assertFalse(output.exists())

    @unittest.skipUnless((ROOT / 'build/probe/BOOTX64.EFI').exists(), 'Run make probe for package integration')
    def test_real_probe_package(self):
        with tempfile.TemporaryDirectory() as directory:
            output = package(Path(directory) / 'usb')
            self.assertTrue((output / 'RTX30-PROBE').is_dir())
            self.assertEqual((output / 'EFI/BOOT/BOOTX64.EFI').read_bytes(),
                             (ROOT / 'build/probe/BOOTX64.EFI').read_bytes())
            self.assertTrue((output / 'LICENSES/EDK2-BSD-2-Clause-Patent.txt').is_file())


if __name__ == '__main__':
    unittest.main()
