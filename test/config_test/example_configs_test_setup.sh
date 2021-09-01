#!/bin/bash

set -e

DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"
tar -xvf "$TEST_SRCDIR"/"$EXAMPLE_CONFIGS_TAR_PATH" -C "$DIR"

# find uses full path to prevent using Windows find on Windows.
/usr/bin/find "$DIR" -type f | grep -c .yaml > "$TEST_TMPDIR"/config-file-count.txt
