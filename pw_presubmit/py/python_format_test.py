# Copyright 2024 The Pigweed Authors
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
"""Tests for Python formatters."""

import importlib.resources
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from format_testing_utils import CapturingToolRunner
from pw_presubmit.format.python import BlackFormatter


_TEST_DATA_FILES = importlib.resources.files('pw_presubmit.format.test_data')
_TEST_SRC_FILE = _TEST_DATA_FILES / 'python_test_data.py'
_TEST_GOLDEN = _TEST_DATA_FILES / 'python_test_data_golden.py'
_TEST_MALFORMED = _TEST_DATA_FILES / 'malformed_file.txt'
_BLACK_CONFIG_PATH = _TEST_DATA_FILES / 'black_config.toml'


class TestBlackFormatter(unittest.TestCase):
    """Tests for the BlackFormatter."""

    maxDiff = None

    def test_check_file(self):
        tool_runner = CapturingToolRunner()
        formatter = BlackFormatter(_BLACK_CONFIG_PATH)
        formatter.run_tool = tool_runner

        result = formatter.format_file_in_memory(
            _TEST_SRC_FILE, _TEST_SRC_FILE.read_bytes()
        )
        self.assertTrue(result.ok)
        self.assertEqual(result.error_message, None)
        self.assertMultiLineEqual(
            result.formatted_file_contents.decode(), _TEST_GOLDEN.read_text()
        )

        self.assertEqual(
            tool_runner.command_history.pop(0),
            ' '.join(
                (
                    'black',
                    '--config',
                    str(_BLACK_CONFIG_PATH),
                    '-q',
                    '-',
                )
            ),
        )

    def test_check_file_error(self):
        tool_runner = CapturingToolRunner()
        formatter = BlackFormatter(_BLACK_CONFIG_PATH)
        formatter.run_tool = tool_runner

        result = formatter.format_file_in_memory(
            _TEST_MALFORMED, _TEST_MALFORMED.read_bytes()
        )
        self.assertFalse(result.ok)
        self.assertEqual(result.formatted_file_contents, b'')
        self.assertTrue(result.error_message.startswith('error: cannot format'))

        self.assertEqual(
            tool_runner.command_history.pop(0),
            ' '.join(
                (
                    'black',
                    '--config',
                    str(_BLACK_CONFIG_PATH),
                    '-q',
                    '-',
                )
            ),
        )

    def test_fix_file(self):
        """Tests that formatting is properly applied to files."""

        tool_runner = CapturingToolRunner()
        formatter = BlackFormatter(_BLACK_CONFIG_PATH)
        formatter.run_tool = tool_runner

        with TemporaryDirectory() as temp_dir:
            file_to_fix = Path(temp_dir) / _TEST_SRC_FILE.name
            file_to_fix.write_bytes(_TEST_SRC_FILE.read_bytes())

            malformed_file = Path(temp_dir) / _TEST_MALFORMED.name
            malformed_file.write_bytes(_TEST_MALFORMED.read_bytes())

            errors = list(formatter.format_files([file_to_fix, malformed_file]))

            # Should see three separate commands, one where we try to format
            # both files together, and then the fallback logic that formats
            # them individually to isolate errors.
            self.assertEqual(
                tool_runner.command_history.pop(0),
                ' '.join(
                    (
                        'black',
                        '--config',
                        str(_BLACK_CONFIG_PATH),
                        '-q',
                        str(file_to_fix),
                        str(malformed_file),
                    )
                ),
            )

            self.assertEqual(
                tool_runner.command_history.pop(0),
                ' '.join(
                    (
                        'black',
                        '--config',
                        str(_BLACK_CONFIG_PATH),
                        '-q',
                        str(file_to_fix),
                    )
                ),
            )

            self.assertEqual(
                tool_runner.command_history.pop(0),
                ' '.join(
                    (
                        'black',
                        '--config',
                        str(_BLACK_CONFIG_PATH),
                        '-q',
                        str(malformed_file),
                    )
                ),
            )

            # Check good build file.
            self.assertMultiLineEqual(
                file_to_fix.read_text(), _TEST_GOLDEN.read_text()
            )

            # Check malformed file.
            self.assertEqual(len(errors), 1)
            malformed_files = [malformed_file]
            for file_path, error in errors:
                self.assertIn(file_path, malformed_files)
                self.assertFalse(error.ok)
                self.assertTrue(
                    error.error_message.startswith('error: cannot format')
                )


if __name__ == '__main__':
    unittest.main()
