"""Tests the api version header file generation.
"""
import os
import pathlib
import string
import tempfile
import unittest

import generate_api_version_header
import utils


class GenerateApiVersionHeaderTest(unittest.TestCase):
    EXPECTED_TEMPLATE = string.Template(
        """#pragma once
#include "common/version/api_version_struct.h"

namespace Envoy {

constexpr ApiVersion api_version = {$major, $minor, $patch};
constexpr ApiVersion oldest_api_version = {$oldest_major, $oldest_minor, $oldest_patch};

} // namespace Envoy""")

    def setUp(self):
        # Using mkstemp instead of NamedTemporaryFile because in windows NT or later
        # the created NamedTemporaryFile cannot be reopened again (see comment in:
        # https://docs.python.org/3.9/library/tempfile.html#tempfile.NamedTemporaryFile)
        self._temp_fd, self._temp_fname = tempfile.mkstemp(text=True)

    def tearDown(self):
        # Close and delete the temp file.
        os.close(self._temp_fd)
        pathlib.Path(self._temp_fname).unlink()

    # General success pattern when valid file contents is detected.
    def successful_test_template(
            self, output_string, current_version: utils.ApiVersion,
            oldest_version: utils.ApiVersion):
        pathlib.Path(self._temp_fname).write_text(output_string)

        # Read the string from the file, and parse the version.
        output = generate_api_version_header.generate_header_file(self._temp_fname)
        expected_output = GenerateApiVersionHeaderTest.EXPECTED_TEMPLATE.substitute({
            'major': current_version.major,
            'minor': current_version.minor,
            'patch': current_version.patch,
            'oldest_major': oldest_version.major,
            'oldest_minor': oldest_version.minor,
            'oldest_patch': oldest_version.patch
        })
        self.assertEqual(expected_output, output)

    # General failure pattern when invalid file contents is detected.
    def failed_test_template(self, output_string, assertion_error_type):
        pathlib.Path(self._temp_fname).write_text(output_string)

        # Read the string from the file, and expect version parsing to fail.
        with self.assertRaises(
                assertion_error_type,
                msg='The call to generate_header_file should have thrown an exception'):
            generate_api_version_header.generate_header_file(self._temp_fname)

    def test_valid_version(self):
        self.successful_test_template('1.2.3', utils.ApiVersion(1, 2, 3), utils.ApiVersion(1, 1, 0))

    def test_valid_version_newline(self):
        self.successful_test_template(
            '3.2.1\n', utils.ApiVersion(3, 2, 1), utils.ApiVersion(3, 1, 0))

    def test_invalid_version_string(self):
        self.failed_test_template('1.2.abc3', ValueError)

    def test_invalid_version_partial(self):
        self.failed_test_template('1.2.', ValueError)

    def test_empty_file(self):
        # Not writing anything to the file
        self.failed_test_template('', AssertionError)

    def test_invalid_multiple_lines(self):
        self.failed_test_template('1.2.3\n1.2.3', AssertionError)

    def test_valid_oldest_api_version(self):
        expected_latest_oldest_pairs = [(utils.ApiVersion(3, 2, 2), utils.ApiVersion(3, 1, 0)),
                                        (utils.ApiVersion(4, 5, 30), utils.ApiVersion(4, 4, 0)),
                                        (utils.ApiVersion(1, 1, 5), utils.ApiVersion(1, 0, 0)),
                                        (utils.ApiVersion(2, 0, 3), utils.ApiVersion(2, 0, 0))]

        for latest_version, expected_oldest_version in expected_latest_oldest_pairs:
            self.assertEqual(
                expected_oldest_version, utils.compute_oldest_api_version(latest_version))


if __name__ == '__main__':
    unittest.main()
