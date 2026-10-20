"""Strict RTX30 probe report parser and evidence-bound board profile generator."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from gpu_family import chip_name

REGISTERS = (0, 0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828,
             0x823804, 0x82381c, 0x823820, 0x1183a4, 0x625f04, 0x1fa7c8,
             0x1fa82c, 0x1fa830, 0x824148, 0x8241e0)
ARCHITECTURES = {0x2203: 'GA102', 0x2204: 'GA102', 0x2206: 'GA102'}
FIELDS = {
    'REPORT': {'format'},
    'OUTPUT': {'loaded_image_status', 'device_path_status', 'path_node', 'output',
               'usb_filesystem_status', 'usb_open_volume_status', 'probe_directory_status',
               'run_directory_status', 'run_slot', 'pci_enumeration_status'},
    'SUMMARY': {'matching_cards', 'gop_enumeration_status'},
    'PCI': {'location_status', 'segment', 'bus', 'device', 'function', 'identity',
            'class_revision', 'subsystem', 'detail_supported', 'device_path_status', 'path_node'} |
           {f'config_{i:02x}' for i in range(0, 256, 4)},
    'BAR': {'pci_index', 'bar_index', 'resource_type', 'base', 'length', 'status'},
    'GOP': {'index', 'console_handle', 'mode', 'framebuffer_base', 'framebuffer_size',
            'width', 'height', 'pixels_per_scanline', 'pixel_format', 'device_path_status', 'path_node'},
    'DETAIL': {'inventory_save_status'},
    'TARGET': {'pci_index', 'device_path_status', 'path_node', 'collect_status', 'identity',
               'subsystem', 'bar0_status', 'bar0_length', 'bar0_base', 'memory_decode_enabled'},
    'REG': {'register', 'status', 'value'},
    'DUMPS': {'rom_image_size', 'rom_image_save_status', 'rom_image', 'prom_allocation_status',
              'prom_read_status', 'prom_save_status', 'prom_sha256', 'prom_size', 'prom'},
    'SKIPPED': {'reason'},
    'END': {'gpu_writes', 'gpu_resets', 'boot_variable_changes'},
}
TEXT_FIELDS = {'output', 'rom_image', 'prom', 'reason', 'gpu_writes', 'gpu_resets',
               'boot_variable_changes', 'prom_sha256', 'path_node'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def number(section, key, maximum=0xffffffffffffffff):
    value = section.get(key, '')
    if not re.fullmatch(r'0x[0-9a-f]{16}', value) or int(value, 16) > maximum:
        raise ValueError(f'Missing/invalid {key}')
    return int(value, 16)


def parse(data):
    if not data or len(data) >= 32768:
        raise ValueError('Empty or oversized report')
    sections = {}
    current = None
    for line in data.decode('ascii').splitlines():
        if re.fullmatch(r'\[(REPORT|OUTPUT|SUMMARY|DETAIL|TARGET|DUMPS|SKIPPED|END|(?:PCI|BAR|GOP|REG):[0-9a-f]{4})\]', line):
            name = line[1:-1]
            if name in sections:
                raise ValueError('Duplicate section: ' + name)
            current = sections[name] = {}
        else:
            if current is None or '=' not in line:
                raise ValueError('Malformed report line')
            key, value = line.split('=', 1)
            if key not in FIELDS[name.split(':')[0]] or not value:
                raise ValueError('Malformed report field')
            if key == 'path_node':
                if not re.fullmatch(r'(?:[0-9a-f]{2}){4,4096}', value):
                    raise ValueError('Invalid device path')
                raw = bytes.fromhex(value)
                if int.from_bytes(raw[2:4], 'little') != len(raw):
                    raise ValueError('Device path node length mismatch')
                current.setdefault(key, []).append(value)
            elif key in current:
                raise ValueError('Duplicate field: ' + key)
            else:
                current[key] = value
                if key not in TEXT_FIELDS:
                    number(current, key)
                elif key == 'prom_sha256' and not re.fullmatch(r'[0-9a-f]{64}', value):
                    raise ValueError('Malformed PROM hash')
    if next(iter(sections), '') != 'REPORT' or list(sections)[-1:] != ['END']:
        raise ValueError('Missing report boundaries')
    if number(sections['REPORT'], 'format') != 1:
        raise ValueError('Unsupported report format')
    if sections['END'] != dict(gpu_writes='none', gpu_resets='none', boot_variable_changes='none'):
        raise ValueError('Incomplete/read-write report')
    summary = sections.get('SUMMARY', {})
    if number(sections.get('OUTPUT', {}), 'pci_enumeration_status'):
        raise ValueError('PCI enumeration failed')
    count = number(summary, 'matching_cards', 0xffff)
    cards = {name: value for name, value in sections.items() if name.startswith('PCI:')}
    if set(cards) != {f'PCI:{i:04x}' for i in range(count)}:
        raise ValueError('PCI inventory count mismatch')
    for card in cards.values():
        for key, limit in [('segment', 65535), ('bus', 255), ('device', 31), ('function', 7)]:
            number(card, key, limit)
        number(card, 'location_status')
        identity = number(card, 'identity', 0xffffffff)
        subsystem = number(card, 'subsystem', 0xffffffff)
        revision = number(card, 'class_revision', 0xffffffff)
        config = [number(card, f'config_{i:02x}', 0xffffffff) for i in range(0, 256, 4)]
        if identity & 0xffff != 0x10de or revision >> 24 != 3 or (
                config[0], config[2], config[11]) != (identity, revision, subsystem):
            raise ValueError('Inconsistent NVIDIA display PCI snapshot')
        number(card, 'detail_supported', 1)
    expected_bars = {f'BAR:{i * 8 + bar:04x}' for i in range(count) for bar in range(6)}
    if {name for name in sections if name.startswith('BAR:')} != expected_bars:
        raise ValueError('Missing/extra BAR resources')
    for i in range(count):
        for bar in range(6):
            resource = sections[f'BAR:{i * 8 + bar:04x}']
            if number(resource, 'pci_index') != i or number(resource, 'bar_index') != bar:
                raise ValueError('BAR inventory index mismatch')
            if number(resource, 'status') == 0:
                size = number(resource, 'length')
                if not size or number(resource, 'base') > 0xffffffffffffffff - size + 1:
                    raise ValueError('Invalid BAR range')
                number(resource, 'resource_type', 2)
    register_sections = {name for name in sections if name.startswith('REG:')}
    if register_sections and register_sections != {f'REG:{i:04x}' for i in range(len(REGISTERS))}:
        raise ValueError('Incomplete/extra register samples')
    return sections


def generate(report, prom=None):
    report = Path(report).resolve()
    data = report.read_bytes()
    sections = parse(data)
    if number(sections['SUMMARY'], 'matching_cards') != 1:
        raise ValueError('Profile requires exactly one NVIDIA display device')
    card = sections['PCI:0000']
    if number(card, 'location_status'):
        raise ValueError('PCI location unavailable')
    identity = number(card, 'identity')
    subsystem = number(card, 'subsystem')
    target = sections.get('TARGET', {})
    complete = bool(target) and number(target, 'collect_status') == 0
    registers = {}
    if complete:
        if number(sections.get('DETAIL', {}), 'inventory_save_status') != 0:
            raise ValueError('Detail reads without a persisted inventory')
        if (number(target, 'pci_index') != 0 or number(target, 'identity') != identity or
                number(target, 'subsystem') != subsystem):
            raise ValueError('Target differs from inventory')
        for i, offset in enumerate(REGISTERS):
            sample = sections.get(f'REG:{i:04x}', {})
            if number(sample, 'register', 0xffffffff) != offset:
                raise ValueError('Register layout mismatch')
            status = number(sample, 'status')
            value = number(sample, 'value', 0xffffffff)
            valid = status == 0 and value != 0xffffffff and value >> 16 != 0xbadf
            registers[f'{offset:06x}'] = dict(value=f'{value:08x}', valid=valid)
            complete &= valid
        complete &= number(target, 'bar0_status') == 0 and number(target, 'memory_decode_enabled', 1) == 1
        bar_size = number(target, 'bar0_length')
        if bar_size and number(target, 'bar0_base') > 0xffffffffffffffff - bar_size + 1:
            raise ValueError('Invalid BAR0 range')
        complete &= bar_size >= 0x400000
    dumps = sections.get('DUMPS', {})
    prom_info = None
    inputs = dict(report=dict(path=str(report), sha256=sha(data)))
    if prom:
        prom = Path(prom).resolve()
        raw = prom.read_bytes()
        if len(raw) != 1048576 or raw[:2] != b'\x55\xaa':
            raise ValueError('PROM must be the 1 MiB window with 55aa signature')
        digest = sha(raw)
        if (dumps.get('prom_sha256') != digest or number(dumps, 'prom_size') != len(raw) or
                number(dumps, 'prom_read_status') != 0 or number(dumps, 'prom_save_status') != 0):
            raise ValueError('PROM does not match successful report capture')
        prom_info = dict(size=len(raw), signature='55aa', sha256=digest)
        inputs['prom'] = dict(path=str(prom), sha256=digest)
    complete = bool(complete and prom_info)
    observed = registers.get('000000')
    architecture = (chip_name(int(observed['value'], 16)) if observed and observed['valid']
                    else ARCHITECTURES.get(identity >> 16, 'unknown'))
    return dict(schema=1, target=dict(vendorId='10de', deviceId=f'{identity >> 16:04x}',
        subsystemVendorId=f'{subsystem & 0xffff:04x}', subsystemDeviceId=f'{subsystem >> 16:04x}',
        pciAddress='{segment:04x}:{bus:02x}:{device:02x}.{function:x}'.format(
            **{key: number(card, key) for key in ('segment', 'bus', 'device', 'function')}),
        architecture=architecture, architectureVerified=False),
        hardware=dict(chipId=registers.get('000000'), classRevision=card['class_revision'], bar0Size=target.get('bar0_length'),
            bar0Base=target.get('bar0_base'), vramSize=None,
            promSha256=prom_info['sha256'] if prom_info else None, prom=prom_info),
        preboot=dict(displayRoute='unverified', gop=[value for name, value in sections.items()
            if name.startswith('GOP:')], registers=registers),
        inputs=inputs, validation=dict(probeComplete=complete, firmwareCompatible=False,
            unlockApproved=False, hardwareTested=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', required=True)
    parser.add_argument('--prom', default='')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    try:
        profile = generate(args.report, args.prom)
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('x', encoding='utf-8') as stream:
            json.dump(profile, stream, indent=2)
            stream.write('\n')
        print(f'Created {output}; unlockApproved=false (independent validation required).')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Profile refused: {error}\n')
