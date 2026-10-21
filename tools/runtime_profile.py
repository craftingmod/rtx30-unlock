"""Compile checked Probe evidence into the fixed v17 EFI profile format."""
import argparse
import hashlib
from pathlib import Path
import struct

from check_profile import check

LAYOUT = ('625f04', '1fa7c8', '1fa82c', '1fa830', '1fa824', '1fa828')


def encode(profile):
    target, hardware = profile['target'], profile['hardware']
    registers = profile['preboot']['registers']
    reg = lambda key: int(registers[key]['value'], 16)
    payload = struct.pack('<8s10IQ6I32s32s', b'GUPROF17', 1, 176,
                          int(target['deviceId'] + target['vendorId'], 16),
                          int(target['subsystemDeviceId'] + target['subsystemVendorId'], 16),
                          reg('000000'), int(hardware['classRevision'], 16),
                          reg('823804'), reg('82381c'), reg('823820'), reg('1183a4'),
                          int(hardware['bar0Size'], 16), *(reg(key) for key in LAYOUT),
                          bytes.fromhex(hardware['promSha256']),
                          bytes.fromhex(profile['inputs']['report']['sha256']))
    assert len(payload) == 144
    return payload + hashlib.sha256(payload).digest()


def compile_profile(source, output):
    data = encode(check(source, experimental=True))
    # An exclusive create keeps an earlier experiment's profile intact.
    with Path(output).open('xb') as stream:
        stream.write(data)
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        compile_profile(args.profile, args.output)
        print('Wrote v17 PROFILE.BIN; evidence checked, hardware compatibility unverified.')
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f'Runtime profile refused: {error}\n')
