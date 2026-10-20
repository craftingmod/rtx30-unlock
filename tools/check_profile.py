"""Reject unapproved profiles and boards not implemented by the current UEFI port."""
import argparse
import json
from pathlib import Path
from probe_profile import generate
from gpu_family import chip_name, pci_candidate

LEGACY_PROM = 'f3dc8f07b0b8c9856d192df172dfa7f7f84ac17ef6edfdb448ad7fa94efd2789'


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def check(path, experimental=False):
    profile = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique)
    if type(profile.get('schema')) is not int or profile['schema'] != 1:
        raise ValueError('Unsupported profile schema')
    validation = profile.get('validation', {})
    for key in (('probeComplete',) if experimental else
                ('probeComplete', 'firmwareCompatible', 'unlockApproved', 'hardwareTested')):
        if validation.get(key) is not True:
            raise ValueError('Independent validation/approval required: ' + key)
    inputs = profile['inputs']
    captured = generate(inputs['report']['path'], inputs['prom']['path'])
    if not captured['validation']['probeComplete']:
        raise ValueError('Incomplete read-only evidence')
    if experimental:
        for key in ('schema', 'target', 'hardware', 'preboot', 'inputs'):
            if profile.get(key) != captured[key]:
                raise ValueError('Profile differs from its original evidence: ' + key)
        registers = captured['preboot']['registers']
        chip = int(registers['000000']['value'], 16)
        identity = int(captured['target']['deviceId'] + captured['target']['vendorId'], 16)
        if (not pci_candidate(identity) or chip_name(chip) == 'unknown' or
                captured['target']['architecture'] != chip_name(chip)):
            raise ValueError('Experimental firmware path requires an observed RTX 30 GA10x chip')
        fb = int(registers['1183a4']['value'], 16)
        size = int(captured['hardware']['bar0Size'], 16)
        if not 256 <= fb <= 0x100000 or size < 0x842000 or size & (size - 1):
            raise ValueError('Invalid framebuffer/BAR geometry')
        if (int(registers['1fa828']['value'], 16) >> 4 or
                not int(registers['1fa7c8']['value'], 16) & 1):
            raise ValueError('WPR2 active or MMU layout unavailable')
        plm, boot, cpu = (int(registers[key]['value'], 16) for key in ('118128', '118234', '110100'))
        if not plm & 1 or boot & 0xff != 0xff or not cpu & 0x10:
            raise ValueError('GFW cold state is not complete')
        return captured
    # Only these three fields come from independent board/display validation.
    captured['target']['architectureVerified'] = True
    captured['hardware']['vramSize'] = 10737418240
    captured['preboot']['displayRoute'] = 'intel-uhd770'
    for key in ('schema', 'target', 'hardware', 'preboot', 'inputs'):
        if profile.get(key) != captured[key]:
            raise ValueError('Profile differs from its original evidence: ' + key)
    target, hw = captured['target'], captured['hardware']
    if (target['deviceId'], target['subsystemVendorId'], target['subsystemDeviceId'],
            target['pciAddress'], target['architecture']) != (
            '2206', '1462', '3890', '0000:01:00.0', 'GA102'):
        raise ValueError('Board/layout has no validated UEFI unlock implementation')
    if (hw['promSha256'] != LEGACY_PROM or hw['bar0Size'] != '0x0000000001000000' or
            hw['bar0Base'] != '0x0000000084000000' or
            hw['chipId'] != dict(value='b72000a1', valid=True)):
        raise ValueError('PROM/chip/BAR differs from implemented runtime checks')
    registers = captured['preboot']['registers']
    for offset, values in {'823804': ('ffffff8f',), '82381c': ('46254040',),
                           '823820': ('00000002', '00000003'), '1183a4': ('00002800',)}.items():
        if registers[offset]['value'] not in values or not registers[offset]['valid']:
            raise ValueError('Unvalidated initial register: ' + offset)
    if int(registers['1fa828']['value'], 16) >> 4:
        raise ValueError('WPR2 is already active')
    return profile


def header(profile):
    target = profile['target']
    registers = profile['preboot']['registers']
    values = dict(GU_PROFILE_BUILD='1',
        GU_BOARD_ID='0x' + target['deviceId'] + target['vendorId'] + 'U',
        GU_BOARD_SUBSYSTEM='0x' + target['subsystemDeviceId'] + target['subsystemVendorId'] + 'U',
        GU_BOARD_CHIP='0x' + registers['000000']['value'] + 'U',
        GU_TARGET_PCI_ID='GU_BOARD_ID',
        GU_PROM_SHA256='"' + profile['hardware']['promSha256'] + '"',
        GU_BAR_SIZE=profile['hardware']['bar0Size'] + 'ULL')
    for macro, key in [('GU_INITIAL_PLM', '823804'), ('GU_INITIAL_SS0', '82381c'),
                       ('GU_INITIAL_SS1', '823820'), ('GU_FB_MIB', '1183a4')]:
        values[macro] = '0x' + registers[key]['value'] + 'U'
    values['GU_CLASS_REVISION'] = profile['hardware']['classRevision'] + 'U'
    layout = ('625f04', '1fa7c8', '1fa82c', '1fa830', '1fa824', '1fa828')
    values['GU_LAYOUT_OFFSETS'] = '{' + ','.join('0x' + key + 'U' for key in layout) + '}'
    values['GU_LAYOUT_VALUES'] = '{' + ','.join('0x' + registers[key]['value'] + 'U' for key in layout) + '}'
    return '/* Generated from verified probe evidence; do not edit. */\n' + ''.join(
        f'#define {key} {value}\n' for key, value in values.items())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile')
    parser.add_argument('--experimental', action='store_true',
                        help='Accept unapproved RTX 30 GA10x evidence; V67 is experimental; runtime safety checks remain enabled')
    parser.add_argument('--header', type=Path)
    args = parser.parse_args()
    try:
        profile = check(args.profile, args.experimental)
        if args.header:
            if not args.experimental:
                raise ValueError('Generated headers currently require experimental mode')
            args.header.parent.mkdir(parents=True, exist_ok=True)
            args.header.write_text(header(profile), encoding='ascii')
        print(('EXPERIMENTAL RTX30 GA10x: V67 hardware compatibility unverified; ' if args.experimental else '') +
              'Profile evidence checked; runtime safety checks remain mandatory.')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Unlock build refused: {error}\n')
