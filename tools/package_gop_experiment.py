"""Stage the standalone GOP cycle experiment into a new local USB folder."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'unlock'))
from verify_pe import inspect


def package(output, root=ROOT, assets=None, profile=None):
    root = Path(root).resolve()
    output = Path(output).absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Choose a new output folder in an existing parent')
    build = root / 'build/gop-experiment'
    verified = inspect(build / 'BOOTX64.EFI', build / 'BOOTX64.map')
    recorded = json.loads((build / 'pe-verification.json').read_text(encoding='utf-8'))
    manifest = json.loads((build / 'build-manifest.json').read_text(encoding='utf-8-sig'))
    if verified != recorded or verified['sha256'].upper() != manifest['EfiSha256'].upper():
        raise ValueError('EFI differs from its build verification; rebuild first')
    sources = manifest['Sources']
    mode = manifest.get('Mode', 'cycle')
    if mode not in ('cycle', 'tree-read-only', 'leaf-member', 'full-flr', 'full-unlock'):
        raise ValueError('Unknown experiment mode')
    required = {'experiments/gop/app.c',
                'scripts/build-gop-experiment.ps1', 'unlock/runtime.c', 'unlock/format.c'}
    required.add({'tree-read-only': 'experiments/gop/tree.c', 'leaf-member': 'experiments/gop/leaf.c', 'cycle': 'experiments/gop/cycle.c', 'full-flr': 'experiments/gop/full.c', 'full-unlock': 'experiments/gop/full.c'}[mode])
    if mode in ('full-flr', 'full-unlock'):
        required.update({'experiments/gop/leaf.c', 'experiments/gop/reset.c'})
    if mode == 'full-unlock':
        required.update({'unlock/app.c', 'unlock/app_io.c', 'unlock/port.c', 'unlock/assets.json',
                         'unlock/vendor/unlock_610.c', 'experiments/gop/patch.h',
                         'experiments/gop/visual.c', 'experiments/gop/visual.h'})
        if manifest.get('ExternalProfile'):
            required.update({'unlock/runtime_profile.c', 'unlock/runtime_profile.h',
                             'tools/runtime_profile.py'})
        else:
            required.update({'build/gop-experiment/profile.h', 'build/gop-experiment/board.json'})
    elif assets is not None:
        raise ValueError('Firmware assets are only used by full-unlock mode')
    if manifest.get('WindowsHandoff'):
        if mode != 'full-unlock':
            raise ValueError('Windows handoff requires full-unlock mode')
        required.update({'experiments/gop/windows.c', 'experiments/gop/windows.h',
                         'experiments/gop/test_windows.c', 'experiments/gop/test-windows.cmd',
                         'experiments/gop/windows-check.ps1', 'analysis/compute-benchmark.cu',
                         'analysis/build-benchmark.cmd', 'results/benchmark.json'})
    if manifest.get('Production'):
        if not manifest.get('WindowsHandoff') or mode != 'full-unlock':
            raise ValueError('Production requires Windows handoff and full-unlock')
        required.update({'experiments/gop/test-production.cmd', 'experiments/gop/test_quiet_io.c'})
    if manifest.get('SsdExperiment'):
        if not manifest.get('Production') or not manifest.get('WindowsHandoff') or mode != 'full-unlock':
            raise ValueError('SSD experiment requires production Windows handoff')
        required.update({'unlock/boot_source.h', 'experiments/gop/test-ssd.cmd', 'experiments/gop/test_ssd_flow.c'})
    profile_data = None
    if manifest.get('ExternalProfile'):
        if not manifest.get('SsdExperiment'):
            raise ValueError('External profile requires the SSD experiment flow')
        if profile is None:
            raise ValueError('External-profile package requires --profile board.json')
        from runtime_profile import encode
        from check_profile import check
        profile_data = encode(check(profile, experimental=True))
    elif profile is not None:
        raise ValueError('--profile requires an external-profile EFI')
    if not required.issubset({entry['Path'] for entry in sources}):
        raise ValueError('Incomplete experiment source manifest')
    with tempfile.TemporaryDirectory(prefix='.gop-stage-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'usb'
        boot = stage / 'EFI/BOOT'
        boot.mkdir(parents=True)
        marker = stage / 'RTX30-GOP-TEST'
        marker.mkdir()
        (marker / 'README.txt').write_text(
            'Keep this directory on the USB root. The EFI requires it before any detach.\n'
            'Per-boot diagnostics are saved in RUN00 through RUN99.\n', encoding='utf-8')
        if manifest.get('Production'):
            (marker / 'README.txt').write_text(
                'Production: automatic unlock and Windows handoff after checks.\n'
                'The SSD experiment build also accepts GPT EFI System Partitions.\n'
                'No RUN folders or reports are created by default.\n'
                'Create an empty DEBUG.ON in this directory for detailed reports and manual U approval.\n'
                'Remove DEBUG.ON after diagnosis to stop log accumulation.\n', encoding='utf-8')
        if profile_data is not None:
            (marker / 'PROFILE.BIN').write_bytes(profile_data)
            # First experiments ship with reports and explicit approval enabled.
            (marker / 'DEBUG.ON').touch()
            with (marker / 'README.txt').open('a', encoding='utf-8') as instructions:
                instructions.write('v17: PROFILE.BIN is mandatory on this EFI volume. Missing/invalid profiles stop before GPU detach.\n'
                                   'Replace PROFILE.BIN with tools/runtime_profile.py output for another captured environment.\n'
                                   'DEBUG.ON is included for the first test; U is required even on the already-applied skip path.\n')
        shutil.copyfile(build / 'BOOTX64.EFI', boot / 'BOOTX64.EFI')
        if mode == 'full-unlock':
            asset_target = stage / 'RTX3080-UNLOCK/ASSETS'
            asset_target.mkdir(parents=True)
            (asset_target / 'README.txt').write_text(
                'Copy the five verified unlock/assets.json files here before boot.\n'
                'The EFI checks sizes and SHA256 before U and before driver detach.\n'
                'Transaction details: RTX3080-UNLOCK/RUNxx/REPORT.TXT.\n', encoding='utf-8')
            if assets is not None:
                for spec in json.loads((root / 'unlock/assets.json').read_text(encoding='utf-8')):
                    source = Path(assets) / spec['Name']
                    data = source.read_bytes()
                    if len(data) != spec['Bytes'] or hashlib.sha256(data).hexdigest() != spec['Sha256'].lower():
                        raise ValueError('Firmware asset mismatch: ' + spec['Name'])
                    (asset_target / spec['Name']).write_bytes(data)
                license_path = Path(assets) / 'NVIDIA-LICENSE'
                if license_path.is_file():
                    shutil.copyfile(license_path, asset_target / 'NVIDIA-LICENSE')
        if inspect(boot / 'BOOTX64.EFI', build / 'BOOTX64.map') != verified:
            raise ValueError('EFI changed during packaging; rebuild first')
        for entry in sources:
            name = entry['Path']
            path = root / name
            if Path(name).is_absolute() or root not in path.resolve().parents or path.is_symlink():
                raise ValueError('Invalid source path')
            data = path.read_bytes()
            if hashlib.sha256(data).hexdigest().upper() != entry['Sha256'].upper():
                raise ValueError('Source changed after build: ' + name)
            target = stage / 'SOURCE' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        if manifest.get('WindowsHandoff'):
            windows = stage / 'WINDOWS'
            windows.mkdir()
            for source, name in (('experiments/gop/windows-check.ps1', 'check.ps1'),
                                 ('analysis/compute-benchmark.cu', 'compute-benchmark.cu'),
                                 ('analysis/build-benchmark.cmd', 'build-benchmark.cmd'),
                                 ('results/benchmark.json', 'historical-benchmark.json')):
                shutil.copyfile(stage / 'SOURCE' / source, windows / name)
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
            shutil.copyfile(root / name, stage / name)
            shutil.copyfile(root / name, stage / 'SOURCE' / name)
        shutil.copytree(root / 'LICENSES', stage / 'LICENSES')
        shutil.copytree(root / 'LICENSES', stage / 'SOURCE/LICENSES')
        shutil.copyfile(root / 'experiments/gop/README.md', stage / 'README.md')
        for name in ('build-manifest.json', 'pe-verification.json', 'BOOTX64.map'):
            shutil.copyfile(build / name, stage / name)
        stage.rename(output)
    return output


def archive(directory, output):
    directory, output = Path(directory), Path(output).absolute()
    if directory.resolve() in output.resolve().parents or directory.resolve() == output.resolve():
        raise ValueError('ZIP output must be outside the package directory')
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError('Choose a new ZIP file in an existing parent')
    required = ('EFI/BOOT/BOOTX64.EFI', 'RTX30-GOP-TEST/README.txt')
    for name in required:
        if not (directory / name).is_file():
            raise ValueError('Incomplete USB package: ' + name)
    with tempfile.TemporaryDirectory(prefix='.gop-zip-', dir=output.parent) as temporary:
        staged = Path(temporary) / 'usb.zip'
        with zipfile.ZipFile(staged, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(directory.rglob('*')):
                if path.is_file():
                    bundle.write(path, path.relative_to(directory).as_posix())
        with zipfile.ZipFile(staged) as bundle:
            if bundle.testzip() is not None:
                raise ValueError('ZIP integrity verification failed')
            for name in required:
                if bundle.read(name) != (directory / name).read_bytes():
                    raise ValueError('ZIP content verification failed: ' + name)
        staged.rename(output)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    parser.add_argument('--zip', help='Also write and verify this new ZIP file')
    parser.add_argument('--assets', type=Path, help='Verified firmware folder for full-unlock mode')
    parser.add_argument('--profile', type=Path, help='Checked board.json to ship with a common external-profile EFI')
    args = parser.parse_args()
    try:
        if args.zip and (Path(args.zip).exists() or Path(args.zip).is_symlink()):
            raise ValueError('ZIP output already exists')
        prepared = package(args.output, assets=args.assets, profile=args.profile)
        print('Prepared ' + str(prepared))
        if args.zip:
            print('Verified ZIP ' + str(archive(prepared, args.zip)))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'GOP experiment package refused: {error}\n')
