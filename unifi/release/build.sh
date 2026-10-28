#!/bin/sh
# Builds the release images for one board:
#   unifi/release/build.sh [MACHINE] [CONFIG]
# Output in release/: the image for switches already running RTLPlayground,
# the OEM web-upgrade image for switches on stock firmware, and SHA256SUMS.
set -eu
MACHINE=${1:-SWTG024AS_A_2_0_1_5C_1SFP}
CONFIG=${2:-unifi/release/hc-swtgw215as.txt}
MODEL=${UNIFI_MODEL:-USWED35}
VERSION=${UNIFI_VERSION:-2.1.8.971}
cd "$(dirname "$0")/../.."

make -C unifi/test
make MACHINE="$MACHINE" CONFIG="$CONFIG" UNIFI_MODEL="$MODEL" UNIFI_VERSION="$VERSION" ${MAKE_EXTRA:-}
make -C installer ${MAKE_EXTRA_INSTALLER:-}

TAG=${TAG:-$(git describe --tags --always --dirty)}
mkdir -p release
cp output/rtlplayground.bin "release/rtlplayground-unifi-$TAG-$MACHINE.bin"
cp installer/output/rtlplayground_oem_upgrade.bin "release/rtlplayground-unifi-$TAG-$MACHINE-oem-upgrade.bin"
cd release
rm -f SHA256SUMS
if command -v sha256sum >/dev/null; then
	sha256sum rtlplayground-unifi-"$TAG"-*.bin > SHA256SUMS
else
	shasum -a 256 rtlplayground-unifi-"$TAG"-*.bin > SHA256SUMS
fi
cat SHA256SUMS
