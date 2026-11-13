import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


GENERATOR = Path(__file__).resolve().parents[1] / "make_image.py"
spec = importlib.util.spec_from_file_location("make_g0_image", GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class ImageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.binary = self.root / "firmware.bin"
        self.binary.write_bytes(struct.pack("<II", 0x20009000, 0x08000009) + bytes(16))
        self.source = self.root / "spi_app_protocol.cpp"
        self.source.write_text("static constexpr uint32_t appVersion = 8U;\n")
        self.header = self.root / "generated" / "g0_firmware_image.h"

    def generate(self, *options):
        argv = [str(GENERATOR), "--input", str(self.binary), "--output", str(self.header), *options]
        with patch.object(sys, "argv", argv):
            generator.main()

    def test_source_version_and_incremental_header(self):
        self.generate("--version-source", str(self.source))
        self.assertIn("version = 8;", self.header.read_text())
        self.assertIn("static const unsigned char data[]", self.header.read_text())
        before = self.header.stat().st_mtime_ns
        self.generate("--version-source", str(self.source))
        self.assertEqual(before, self.header.stat().st_mtime_ns)
        image = bytearray(self.binary.read_bytes())
        image[-1] = 1
        self.binary.write_bytes(image)
        original = self.header.read_text()
        self.generate("--version-source", str(self.source))
        self.assertNotEqual(original, self.header.read_text())
        self.assertFalse(self.header.with_suffix(".h.tmp").exists())

    def test_explicit_version(self):
        self.generate("--version", "8")
        self.assertTrue(self.header.exists())

    def test_missing_ambiguous_or_unsupported_source_version(self):
        for text in ["", "static constexpr uint32_t appVersion = 9U;",
                     "static constexpr uint32_t appVersion = 8U;\n" * 2]:
            with self.subTest(source=text):
                self.source.write_text(text)
                with self.assertRaises(SystemExit):
                    self.generate("--version-source", str(self.source))
                self.assertFalse(self.header.exists())

    def test_invalid_binary_does_not_replace_previous_image(self):
        self.generate("--version-source", str(self.source))
        original = self.header.read_bytes()
        for image in [b"short", bytes(128 * 1024 + 1),
                      struct.pack("<II", 0x20009000, 0x08000008) + bytes(16)]:
            with self.subTest(size=len(image)):
                self.binary.write_bytes(image)
                with self.assertRaises(SystemExit):
                    self.generate("--version-source", str(self.source))
                self.assertEqual(original, self.header.read_bytes())

    def test_disabled_build_skips_companion_dependency(self):
        makefile = self.root / "Makefile"
        image_rules = GENERATOR.with_name("image.mk")
        firmware = self.root / "firmware"
        makefile.write_text(
            f"PROJECT_DIR := {firmware}\n"
            f"BUILDDIR := {self.root / 'build'}\n"
            f"GENERATED_DIR := {self.root / 'generated'}\n"
            f"OBJS := {self.root / 'done'}\n"
            "PCHOBJ :=\n"
            ".PHONY: all\nall: $(OBJS)\n"
            "$(OBJS):\n\ttouch $@\n"
            f"include {image_rules}\n"
        )
        for options in [["G0_EXTENSION=no", "G0_FIRMWARE_UPDATE=yes"],
                        ["G0_EXTENSION=yes", "G0_FIRMWARE_UPDATE=no"]]:
            with self.subTest(options=options):
                subprocess.run(["make", "-f", str(makefile), "-j12", *options, "all"],
                               cwd=self.root, check=True, capture_output=True)
                self.assertFalse(firmware.exists())
                self.assertFalse(self.header.exists())


if __name__ == "__main__":
    unittest.main()
