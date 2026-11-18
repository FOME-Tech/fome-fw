"""Check that packaging uses selected ARM tools and propagates selection failures."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FIRMWARE = Path(__file__).resolve().parents[1]
SCRIPT = FIRMWARE / 'config/boards/write_firmware_checksum.sh'


class ChecksumToolsTest(unittest.TestCase):
    def test_tools_and_selection_failure(self):
        with tempfile.TemporaryDirectory(prefix='arm tools ') as tmp:
            root = Path(tmp)
            shim = root / 'shim'
            shim.mkdir()
            prefix = root / 'arm-'
            program = '''#!/usr/bin/python3
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
with open(os.environ['TEST_TOOL_LOG'], 'a') as log:
 log.write(json.dumps([name, sys.argv[1:]]) + '\\n')
if name == 'make':
 if os.environ.get('TEST_MAKE_FAIL'): sys.exit(7)
 print('toolchain diagnostic')
 print(os.environ['TEST_ARM_PREFIX'])
elif name == 'arm-objdump':
 print('0 .vectors 000002a0 08020000 08020000 00010000 2**4')
elif name in ('objcopy', 'objdump'):
 sys.exit(99)
elif name == 'hex2dfu':
 pathlib.Path(sys.argv[sys.argv.index('-b')+1]).touch()
else:
 pathlib.Path(sys.argv[-1]).touch()
'''
            for path in [shim / 'make', shim / 'objcopy', shim / 'objdump',
                         root / 'arm-objcopy', root / 'arm-objdump', root / 'hex2dfu']:
                path.write_text(program)
                path.chmod(0o755)
            log = root / 'calls.jsonl'
            env = dict(os.environ, PATH=str(shim)+':/usr/bin:/bin',
                       HEX2DFU=str(root / 'hex2dfu'), TEST_TOOL_LOG=str(log),
                       TEST_ARM_PREFIX=str(prefix))
            result = subprocess.run(['bash', str(SCRIPT), 'input elf', 'output file'],
                                    cwd=root, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            calls = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual([call[0] for call in calls],
                             ['make', 'arm-objdump', 'arm-objcopy', 'hex2dfu',
                              'arm-objcopy', 'arm-objcopy'])
            self.assertIn('802001C', calls[3][1])
            self.assertTrue((root / 'output file.srec').exists())
            env['TEST_MAKE_FAIL'] = '1'
            log.unlink()
            result = subprocess.run(['bash', str(SCRIPT), 'input elf', 'output file'],
                                    cwd=root, env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(len(log.read_text().splitlines()), 1)

    def test_custom_toolchain_query(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp) / 'cross-'
            Path(str(prefix)+'g++').touch()
            result = subprocess.run(
                ['make', '-s', '-f', 'use_arm_gcc.mk', 'PROJECT_DIR=.',
                 'TRGT='+str(prefix), 'print-arm-toolchain'], cwd=FIRMWARE,
                check=True, capture_output=True, text=True)
            self.assertEqual(result.stdout.splitlines()[-1], str(prefix))


if __name__ == '__main__':
    unittest.main()
