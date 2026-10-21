"""Call the Bun PE verifier from the remaining Python packaging tools."""
import json
from pathlib import Path
import shutil
import subprocess


def verify_pe(efi, map_path, root):
    bun = shutil.which('bun')
    if not bun:
        raise ValueError('Bun is required for PE verification.')
    verifier = Path(root) / 'tools/verify_pe.ts'
    result = subprocess.run(
        [bun, str(verifier), str(efi), str(map_path)],
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise ValueError(result.stderr.strip() or 'PE verification failed.')
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ValueError('PE verifier returned invalid JSON.') from error
