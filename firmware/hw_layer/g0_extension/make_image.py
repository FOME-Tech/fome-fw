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
    version = parser.add_mutually_exclusive_group(required=True)
    version.add_argument("--version", type=int)
    version.add_argument("--version-source", type=Path,
                         help="Read appVersion from the extension SPI implementation")
    args = parser.parse_args()
    image_version = args.version
    if args.version_source is not None:
        versions = re.findall(r"\bstatic\s+constexpr\s+uint32_t\s+appVersion\s*=\s*(\d+)U?\s*;",
                              args.version_source.read_text())
        if len(versions) != 1:
            parser.error("expected exactly one literal appVersion in --version-source")
        image_version = int(versions[0])
    data = args.input.read_bytes()
    if not 8 <= len(data) <= 128 * 1024 or image_version != 8:
        parser.error("expected protocol version 8 and an 8..131072 byte image")
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
