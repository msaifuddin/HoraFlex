#!/bin/sh
# Builds the host tools the image needs on macOS, which has no glibc argp.
#   ARGP=/path/to/argp-standalone tools/macos/host-tools.sh MACHINE
# argp-standalone (https://github.com/argp-standalone/argp-standalone) must be
# built as libargp.a in $ARGP. httpd_sim (a host simulator needing json-c) is
# skipped: build with SUBDIRS= so make does not try to.
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
M=${1:?MACHINE}
: "${ARGP:?set ARGP to an argp-standalone build}"
O="$R/tools/output/$M"
mkdir -p "$O" "$R/installer/output"
cc -Wall -o "$O/injector" "$R/tools/injector.c"
cc -Wall -I"$ARGP" -o "$O/fileadder" "$R/tools/fileadder.c" "$ARGP/libargp.a" -lz
cc -Wall -I"$ARGP" -o "$O/crc_calculator" "$R/tools/crc_calculator.c" "$ARGP/libargp.a"
cc -Wall -I"$ARGP" -o "$O/imagebuilder" "$R/tools/imagebuilder.c" "$ARGP/libargp.a"
cc -Wall -I"$ARGP" -o "$R/installer/output/updatebuilder" "$R/installer/updatebuilder.c" "$ARGP/libargp.a"
