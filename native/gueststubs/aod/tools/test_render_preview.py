import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

TOOL = Path(__file__).with_name('render_preview.py')


class PreviewToolTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.background = self.base / 'background.png'
        self.foreground = self.base / 'foreground.png'
        Image.new('RGB', (1216, 2688), (60, 90, 140)).save(self.background)
        Image.new('RGBA', (1216, 2688), (180, 10, 10, 128)).save(self.foreground)
        self.output = self.base / 'out'

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *extra, output=None):
        return subprocess.run([sys.executable, str(TOOL), '--background', str(self.background),
                               '--foreground', str(self.foreground), '--highest-y', '166',
                               '--density', '520', '--output', str(output or self.output),
                               *extra], capture_output=True, text=True)

    def test_three_image_sizes(self):
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        for name, size in [('vertical.jpg', (442, 507)), ('horizontal_big.jpg', (390, 455)),
                           ('horizontal_small.jpg', (390, 455))]:
            with Image.open(self.output / name) as image:
                self.assertEqual(image.size, size)
                self.assertEqual(image.format, 'JPEG')

    def test_invalid_inputs_do_not_create_output(self):
        for option in [('--density', '0'), ('--highest-y', '-1'), ('--highest-y', '2688')]:
            with self.subTest(option=option):
                self.assertNotEqual(self.run_tool(*option).returncode, 0)
                self.assertFalse(self.output.exists())
        Image.new('RGBA', (3, 4), (0, 0, 0, 0)).save(self.foreground)
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_existing_directory_is_never_overwritten(self):
        self.output.mkdir()
        marker = self.output / 'vertical.jpg'
        marker.write_bytes(b'unchanged')
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertEqual(marker.read_bytes(), b'unchanged')
        self.assertEqual(list(self.output.iterdir()), [marker])

    def test_repository_output_is_rejected(self):
        output = TOOL.parent / 'must-not-be-created-by-test'
        self.assertFalse(output.exists())
        self.assertNotEqual(self.run_tool(output=output).returncode, 0)
        self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
