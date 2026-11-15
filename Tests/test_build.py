#!/usr/bin/env python3
"""Verify incremental builds, configuration invalidation and a complete clean."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildDependencies(unittest.TestCase):
    def test_incremental_build(self):
        with tempfile.TemporaryDirectory(prefix='usb2can-build-test-') as tmp:
            build = Path(tmp) / 'build'
            def make(*args):
                result = subprocess.run(['make', '-s', '-f', 'Make_G431_Candle_WeActStudio',
                                         'BUILD_DIR=' + str(build), '-j4', *args],
                                        cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertNotIn('warning:', result.stdout, result.stdout)
                return result
            make()
            common = build / 'can.o'
            specific = build / 'Candlelight/control.o'
            hal = build / 'STM32Cube/stm32g4xx_hal_pcd.o'
            elf = next(build.glob('*.elf'))
            watch = (common, specific, hal, elf)
            def times():
                return tuple(p.stat().st_mtime_ns for p in watch)
            before = times()
            make()
            self.assertEqual(times(), before, 'unchanged build rebuilt outputs')
            make('-W', 'Source/settings.h')
            after = times()
            self.assertGreater(after[0], before[0])
            self.assertGreater(after[1], before[1])
            before = after
            make('-W', 'Source/Candlelight/control.h')
            self.assertGreater(specific.stat().st_mtime_ns, before[1])
            before = times()
            make('-W', 'STM32/STM32G431xx_Config/stm32g4xx_hal_conf.h')
            self.assertGreater(hal.stat().st_mtime_ns, before[2])
            before = times()
            make('-W', 'STM32/STM32G431xx_Config/STM32G431xx.ld')
            self.assertGreater(elf.stat().st_mtime_ns, before[3])
            self.assertEqual(common.stat().st_mtime_ns, before[0])
            before = times()
            make('USER_CFLAGS=-std=gnu11 -Wall -g -ffunction-sections -fdata-sections -O2 -DBUILD_DEPENDENCY_TEST=1')
            self.assertTrue(all(new > old for new, old in zip(times(), before)))
            make('clean')
            self.assertFalse(build.exists(), 'clean retained objects or libraries')


if __name__ == '__main__':
    unittest.main()
