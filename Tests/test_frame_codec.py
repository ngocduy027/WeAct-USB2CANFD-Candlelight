#!/usr/bin/env python3
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_regressions import ROOT


class FrameCodec(unittest.TestCase):
    def test_wire_records_and_padding(self):
        with tempfile.TemporaryDirectory(prefix='usb2can-codec-') as tmp:
            executable = str(Path(tmp) / 'test')
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I' + str(ROOT / 'Source/Candlelight'),
                            str(ROOT / 'Tests/test_frame_codec.c'),
                            str(ROOT / 'Source/Candlelight/frame_codec.c'),
                            '-o', executable], check=True)
            subprocess.run([executable], check=True)


if __name__ == '__main__':
    unittest.main()
