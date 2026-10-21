"""Create a verified package folder. Does not select or format a drive."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tempfile
import zipfile

from pe_verification import verify_pe


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def source_path(root, name):
    path = PurePosixPath(name)
    if not name or '\\' in name or ':' in name or path.is_absolute() or '..' in path.parts or '.git' in path.parts:
        raise ValueError('Invalid source path: ' + name)
    local = root.joinpath(*path.parts)
    if local.is_symlink() or root not in local.resolve().parents or not local.is_file():
        raise ValueError('Invalid source path: ' + name)
    return local


def prepare(root, assets, output):
    root = root.resolve()
    output = output.absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('Output already exists; choose a new local directory.')
    if not output.parent.is_dir():
        raise ValueError('Output parent must be an existing local directory.')
    source_manifest_bytes = (root / 'SOURCE-MANIFEST.json').read_bytes()
    source_manifest = json.loads(source_manifest_bytes.decode('utf-8-sig'))
    source_files = {}
    source_hashes = {}
    for item in source_manifest['files']:
        name = item['path']
        path = source_path(root, name)
        if name in source_files or digest(path) != item['sha256'].lower():
            raise ValueError('Changed or duplicate source file: ' + name)
        source_files[name] = path
        source_hashes[name] = item['sha256'].lower()

    def verified_source_bytes(name):
        data = source_files[name].read_bytes()
        if hashlib.sha256(data).hexdigest() != source_hashes[name]:
            raise ValueError('Changed source while packaging: ' + name)
        return data

    build = root / 'unlock/build'
    efi = build / 'BOOTX64.EFI'
    manifest = read_json(build / 'build-manifest.json')
    if hashlib.sha256(source_manifest_bytes).hexdigest() != manifest['SourceManifestSha256'].lower():
        raise ValueError('Release source manifest changed; rebuild first.')
    if digest(efi) != manifest['EfiSha256'].lower():
        raise ValueError('EFI hash differs from build manifest; rebuild first.')
    for item in manifest['Sources']:
        name = item['Path'].replace('\\', '/')
        if name not in source_files or digest(source_files[name]) != item['Sha256'].lower():
            raise ValueError('Build source changed; rebuild first: ' + name)
    verify_pe(efi, build / 'BOOTX64.map', root)

    specs = read_json(root / 'unlock/assets.json')
    names = [entry['Name'] for entry in specs]
    if len(names) != 5 or len(set(names)) != 5 or any(not re.fullmatch(r'[a-z0-9_.-]+\.bin', name) for name in names):
        raise ValueError('Invalid asset manifest.')
    if assets is not None:
        assets = assets.resolve()
        for entry in specs:
            path = assets / entry['Name']
            if not path.is_file() or path.is_symlink() or path.stat().st_size != entry['Bytes'] or digest(path) != entry['Sha256'].lower():
                raise ValueError('Missing or changed asset: ' + entry['Name'])

    # All checks finish before creating output. Only an independently staged tree
    # is moved into place, and existing destinations are never reused.
    with tempfile.TemporaryDirectory(prefix='.usb-stage-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'package'
        boot = stage / 'EFI/BOOT'
        payload = stage / 'RTX-UNLOCK'
        asset_target = payload / 'ASSETS'
        notices = payload / 'LICENSES'
        for directory in (boot, asset_target, notices):
            directory.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(efi, boot / 'BOOTX64.EFI')
        if digest(boot / 'BOOTX64.EFI') != manifest['EfiSha256'].lower():
            raise ValueError('EFI changed while copying; rebuild and retry.')
        (notices / 'GPL-2.0.txt').write_bytes(verified_source_bytes('LICENSE'))
        for path in sorted((root / 'LICENSES').iterdir()):
            relative = path.relative_to(root).as_posix()
            if path.is_file() and relative in source_files:
                (notices / path.name).write_bytes(verified_source_bytes(relative))
        (stage / 'USB-README.md').write_bytes(verified_source_bytes('docs/USB-BOOT.md'))
        if assets is None:
            (asset_target / 'REQUIRED-ASSETS.json').write_text(json.dumps(specs, indent=2) + '\n')
        else:
            for name in names:
                shutil.copyfile(assets / name, asset_target / name)
            for entry in specs:
                if digest(asset_target / entry['Name']) != entry['Sha256'].lower():
                    raise ValueError('Asset changed while copying: ' + entry['Name'])
        with zipfile.ZipFile(payload / 'SOURCE.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, path in sorted(source_files.items()):
                archive.writestr(name, verified_source_bytes(name))
            archive.writestr('SOURCE-MANIFEST.json', source_manifest_bytes)
        package = {
            'schema': 1, 'version': 'v0.10-public',
            'efi_sha256': digest(boot / 'BOOTX64.EFI'),
            'assets_included': assets is not None,
            # File completeness does not establish hardware/USB compatibility.
            'ready_for_boot': False,
            'status': 'files-complete-hardware-preflight-required' if assets is not None else 'template-add-required-assets',
            'new_usb_hardware_tested': False,
        }
        (stage / 'PACKAGE.json').write_text(json.dumps(package, indent=2) + '\n')
        sums = ''.join(digest(p) + '  ' + p.relative_to(stage).as_posix() + '\n' for p in sorted(stage.rglob('*')) if p.is_file())
        (stage / 'SHA256SUMS.txt').write_text(sums)
        if output.exists():
            raise ValueError('Output already exists; no files replaced.')
        stage.rename(output)
    return package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--assets', type=Path, help='Directory containing the five matching local assets.')
    parser.add_argument('--output', required=True, type=Path, help='New local directory; do not select a USB drive.')
    args = parser.parse_args()
    try:
        package = prepare(args.project, args.assets, args.output)
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps(package, indent=2))


if __name__ == '__main__':
    main()
