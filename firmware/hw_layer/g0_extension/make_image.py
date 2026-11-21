#!/usr/bin/env python3
"""Create an immutable bundled image from an explicitly selected STM32G071 binary."""
import argparse
import hashlib
from pathlib import Path
import re
import struct

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version-header", type=Path, required=True)
    args = parser.parse_args()
    versions = re.findall(r"^#define G0_FIRMWARE_VERSION (0x[0-9a-fA-F]+|[0-9]+)U?$",
                          args.version_header.read_text(), re.MULTILINE)
    if len(versions) != 1:
        parser.error("expected exactly one G0_FIRMWARE_VERSION in --version-header")
    image_version = int(versions[0], 0)
    data = args.input.read_bytes()
    if not 8 <= len(data) <= 128 * 1024 or not 0 < image_version <= 0xFFFFFFFF:
        parser.error("expected a nonzero 32-bit firmware identity and an 8..131072 byte image")
    sp, entry = struct.unpack_from("<II", data)
    if not (0x20000000 < sp <= 0x20009000 and sp % 8 == 0 and entry & 1
            and 0x08000008 <= (entry & ~1) < 0x08000000 + len(data)):
        parser.error("invalid STM32G071 application vectors")
    rows = [", ".join(f"0x{value:02x}" for value in data[i:i + 16]) for i in range(0, len(data), 16)]
    content = (f"// SHA256: {hashlib.sha256(data).hexdigest()}\n#pragma once\n"
               f"namespace g0_image {{\nstatic constexpr unsigned version = {image_version};\n"
               "static const unsigned char data[] = {\n" + ",\n".join(rows) + "\n};\n}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != content:
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(content)
        temporary.replace(args.output)

if __name__ == "__main__":
    main()
