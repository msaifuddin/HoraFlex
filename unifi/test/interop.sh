#!/bin/sh
# Decodes the frames test_tnbu.c wrote with unifi-stubd's decoder.
# Needs the helper built from ref/tnbuinterop.go inside a unifi-stubd checkout:
#   TNBU_INTEROP=/path/to/tnbuinterop ./interop.sh
set -eu
[ -n "${TNBU_INTEROP:-}" ] || { echo "TNBU_INTEROP not set, skipping"; exit 0; }
out=$("$TNBU_INTEROP" dec ba86f2bbe107c7c57eb5f2690775c712 02:11:22:33:44:55 < tnbu_ours.txt)
echo "$out"
exp=$(printf '0001 7b7d\n0001 %s\n0009 7b7d\n0009 %s' \
	"$(printf '{"model":"USWED35"}' | xxd -p)" "$(printf '{"model":"USWED35"}' | xxd -p)")
[ "$out" = "$exp" ] && echo "interop OK" || { echo "interop MISMATCH"; exit 1; }
