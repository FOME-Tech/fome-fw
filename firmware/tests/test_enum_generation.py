"""Exercise cached enum generation in both firmware and bootloader builds."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

RULES = Path(__file__).resolve().parents[1] / 'fome_generated.mk'


class EnumGenerationTest(unittest.TestCase):
    def test_cached_headers(self):
        for subdir in ('.', 'bootloader'):
            with self.subTest(subdir=subdir), tempfile.TemporaryDirectory() as tmp:
                project = Path(tmp)
                work = project / subdir
                work.mkdir(exist_ok=True)
                inputs = ['integration/LiveData.yaml', 'integration/fome_config.txt',
                          'gen_enum_to_string.sh', 'controllers/algo/rusefi_hw_enums.h',
                          'controllers/algo/rusefi_enums.h', 'controllers/algo/engine_types.h',
                          'controllers/sensors/sensor_type.h', 'controllers/trigger/decoders/sync_edge.h']
                headers = ['controllers/algo/auto_generated_enums.h',
                           'controllers/algo/auto_generated_commonenum.h',
                           'controllers/algo/auto_generated_enginetypes.h',
                           'controllers/sensors/auto_generated_sensor.h',
                           'controllers/trigger/decoders/auto_generated_sync_edge.h']
                for name in inputs + headers:
                    p = project / name
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.touch()
                    os.utime(p, (100, 100))
                (project / '.dep').mkdir()
                (project / 'generated').mkdir()
                for stage, var in [('live_data', 'LIVE_DATA'), ('config', 'CONFIG')]:
                    (project / 'generated' / stage).touch()
                    (project / '.dep' / f'generated_{stage}_outputs.mk').write_text(
                        f'{var}_GENERATED_FILES += generated/{stage}\n')
                (work / 'build').mkdir()
                for name in ['java_tools.cookie', 'generated_live_data.stamp', 'generated_config.stamp']:
                    p = work / 'build' / name
                    p.touch()
                    os.utime(p, (2000000000, 2000000000))
                (work / 'Makefile').write_text(
                    f'all: object pch\nPROJECT_DIR = {project}\nBUILDDIR = build\n'
                    'OBJS = object\nPCHOBJ = pch\n'
                    f'include {RULES}\n')

                def generates():
                    result = subprocess.run(['make', '-n', 'all'], cwd=work,
                                            check=True, capture_output=True, text=True)
                    return './gen_live_documentation.sh' in result.stdout

                self.assertFalse(generates())
                enum = project / headers[0]
                os.utime(enum, (2000000001, 2000000001))  # Header replaced by git pull.
                self.assertTrue(generates())
                os.utime(enum, (100, 100))
                source = project / 'controllers/algo/rusefi_hw_enums.h'
                os.utime(source, (2000000001, 2000000001))
                self.assertTrue(generates())
                os.utime(source, (100, 100))
                enum.unlink()
                self.assertTrue(generates())
                enum.touch()
                os.utime(enum, (100, 100))
                (project / 'generated/config').unlink()
                result = subprocess.run(['make', '-n', 'all'], cwd=work,
                                        check=True, capture_output=True, text=True)
                self.assertIn('./gen_config_board.sh', result.stdout)
                self.assertNotIn('./gen_live_documentation.sh', result.stdout)
                (project / 'generated/config').touch()
                (project / 'generated/live_data').unlink()
                self.assertTrue(generates())
                for manifest in (project / '.dep').glob('*_outputs.mk'):
                    manifest.unlink()
                self.assertTrue(generates())


if __name__ == '__main__':
    unittest.main()
