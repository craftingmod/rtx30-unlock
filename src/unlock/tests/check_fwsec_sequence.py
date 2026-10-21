"""Pinned v0.6 write-sequence regression; host model only, no GPU access."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

root = Path(__file__).resolve().parents[3]
build = root / "unlock/build"
reference = root / "src/unlock/tests/reference/unlock_610_v06.c"
# Same-line attribution notice added to the public v0.6 snapshot; code and line numbers are unchanged.
reference_sha = "1440F0D0C974B8F1D985F48B5852BE7287ABE10BC66D468FCA3833E3CDBC9BAB"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


if sha(reference) != reference_sha:
    raise ValueError("Immutable v0.6 reference changed")
if sys.argv[1:] == ["--prepare"]:
    stage = build / "reference-v06"
    (stage / "vendor").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(reference, stage / "vendor/unlock_610.c")
    # Resolve the archived relative include to the same pragma-once file.
    (stage / "port.h").write_text('#include "../../../src/unlock/port.h"\n', encoding="ascii")
    print("Pinned v0.6 reference prepared")
    sys.exit(0)
if sys.argv[1:]:
    raise ValueError("Unknown arguments")
current = build / "fwsec-current.trace"
baseline = build / "fwsec-v06.trace"
data = current.read_bytes()
if data != baseline.read_bytes():
    raise ValueError("Ordered MMIO/PCI writes differ from v0.6")
if len(data) < 8 or struct.unpack_from("<II", data) != (0x47555431, 5):
    raise ValueError("Incomplete trace header")
expected = [(0, 0), (13, -5), (1, -110), (18, -5), (19, -5)]
at = 8
cases = []
for expected_mode, expected_result in expected:
    mode, result, count = struct.unpack_from("<IiI", data, at)
    at += 12
    if (mode, result) != (expected_mode, expected_result) or count == 0:
        raise ValueError("Unexpected trace scenario/outcome")
    records = [struct.unpack_from("<IIII", data, at + i * 16) for i in range(count)]
    if any(kind not in (0, 1) or (kind == 0 and offset in (0x1101c0, 0x1101c4))
           for kind, width, offset, value in records):
        raise ValueError("Unexpected write type or PIO access")
    at += count * 16
    cases.append({"Scenario": mode, "Result": result, "OrderedWrites": count})
if at != len(data):
    raise ValueError("Trailing or truncated trace")
record = {"AllMatched": True, "HardwareAccess": False,
          "ReferenceCoreSha256": reference_sha,
          "CurrentCoreSha256": sha(root / "src/unlock/vendor/unlock_610.c"),
          "TraceSha256": sha(current), "Scenarios": cases,
          "Scope": "Current versus archived v0.6 core using the SAME current port/compat and simulated EFI services. Not historical adapter equivalence or physical bus-driver transactions. Extra reads/logging may change timing."}
(build / "fwsec-sequence-verification.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
print("5 core write sequences match archived v0.6 using the same current adapter; hardware behavior simulated.")
