#!/bin/bash
#
# Clean-only wrapper around clean_and_test_macos.sh.
#
# Wipes every CorridorKey artifact on the machine (install tree,
# plug-in copies from every AE install, pkgutil receipt, stray
# runtime processes, port file, log). Does NOT install anything
# afterwards — leaves the machine in a verified clean state so you
# can download a fresh installer and test it by hand.
#
# Equivalent to `clean_and_test_macos.sh --clean-only`; this wrapper
# exists so the intent is obvious from the filename.
#

set -e
exec "$(dirname "$0")/clean_and_test_macos.sh" --clean-only "$@"
