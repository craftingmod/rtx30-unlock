"""Stage the read-only EFI into a new local folder; never formats/selects a drive."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from pe_verification import verify_pe

ROOT = Path(__file__).resolve().parents[1]


def package(output, root=ROOT):
    output = Path(output).absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Choose a new output folder in an existing parent')
    build = root / 'build/probe'
    verified = verify_pe(build / 'BOOTX64.EFI', build / 'BOOTX64.map', root)
    recorded = json.loads((build / 'pe-verification.json').read_text(encoding='utf-8'))
    if verified != recorded:
        raise ValueError('Probe differs from its build verification; rebuild first')
    with tempfile.TemporaryDirectory(prefix='.probe-stage-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'usb'
        boot = stage / 'EFI/BOOT'
        boot.mkdir(parents=True)
        (stage / 'RTX30-PROBE').mkdir()
        shutil.copyfile(build / 'BOOTX64.EFI', boot / 'BOOTX64.EFI')
        if verify_pe(boot / 'BOOTX64.EFI', build / 'BOOTX64.map', root) != verified:
            raise ValueError('Probe changed during packaging; rebuild first')
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
            shutil.copyfile(root / name, stage / name)
        shutil.copytree(root / 'LICENSES', stage / 'LICENSES')
        (stage / 'README.txt').write_text(
            'Copy this folder contents to a FAT32 USB and boot EFI/BOOT/BOOTX64.EFI.\n'
            'Reports: RTX30-PROBE/RUNxx/. INVENTORY.TXT precedes MMIO reads.\n'
            'REPORT.TXT is the complete versioned capture. PROM and ROMIMAGE are separate.\n'
            'This program collects read-only diagnostics; it does not unlock the GPU.\n', encoding='utf-8')
        stage.rename(output)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    try:
        print('Prepared ' + str(package(args.output)))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Probe package refused: {error}\n')
