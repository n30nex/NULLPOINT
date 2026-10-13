"""Check native BMP capture completeness and the observed invalid-lighting regression."""
from pathlib import Path
import struct
import sys

folder = Path(sys.argv[1])
for name in ("01-title", "02-arrival", "03-reactor", "04-relay", "05-warden", "06-combat", "07-service", "08-grenade"):
    data = (folder / (name + ".bmp")).read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    assert data[:2] == b"BM" and struct.unpack_from("<H", data, 28)[0] == 24
    assert width >= 320 and abs(height) >= 240
    stride = (width * 3 + 3) & ~3
    assert len(data) >= offset + stride * abs(height)
    black = 0
    for y in range(abs(height)):
        row = data[offset + y * stride:offset + y * stride + width * 3]
        black += sum(row[i:i + 3] == b"\0\0\0" for i in range(0, len(row), 3))
    # These deliberately lit verification views have no large black regions.
    # The bad Fresnel power input previously produced thousands of black pixels.
    assert black < max(100, width * abs(height) // 1000), (name, black)
    print(f"PASS {name}: {width}x{abs(height)}, {black} black pixels")
