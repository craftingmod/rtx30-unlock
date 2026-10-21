import pathlib
import struct
import subprocess
import sys
import tempfile

root=pathlib.Path(__file__).resolve().parents[3]
unlock=root/'unlock'
build=unlock/'build'
original=(build/'BOOTX64.EFI').read_bytes()
pe=struct.unpack_from('<I',original,0x3c)[0]
with tempfile.TemporaryDirectory(prefix='pe-negative-',dir=build) as scratch:
    for name,offset,value in [('machine',pe+4,b'\0\0'),('subsystem',pe+24+68,b'\x03\0'),
                              ('import',pe+24+112+8,b'\x01\0\0\0')]:
        changed=bytearray(original);changed[offset:offset+len(value)]=value
        path=pathlib.Path(scratch)/f'{name}.efi';path.write_bytes(changed)
        result=subprocess.run([sys.executable,'-O',str(unlock/'verify_pe.py'),str(path),
            str(build/'BOOTX64.map'),'--output',str(path.with_suffix('.json'))],capture_output=True,text=True)
        if result.returncode==0: raise RuntimeError(f'Optimized PE verifier accepted malformed {name}')
        print(f'Optimized verifier rejects malformed {name}: PASS')
