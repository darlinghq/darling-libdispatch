#!/bin/bash

# FIXME: this path should probably not be hardcoded, but determining the script path on old Bash like the one macOS ships is weird/difficult
TEST_DIR="/usr/libexec/test/dispatch"

if [ $# -ne 1 ]; then
	echo "Usage: $0 <test-name>"
	exit 1
fi

# Darling doesn't have the `leaks` command yet
export NOLEAKS=1

"${TEST_DIR}/dispatch_bsdtestharness" "${TEST_DIR}/dispatch_test_$1"
