#!/bin/bash -x

# check for whether we're clang or gcc
# setup the right options depending on the environment variables
# run the build

if [ "${AARCH64}" == "1" ]; then
    # convert the arch specifier
    OPTS="${OPTS} --cross-file config/arm/arm64_armv8_linuxapp_gcc"
fi

grep -ri '__ARM_NEON' /usr/lib/
aarch64-linux-gnu-gcc --target-help | grep arm
OPTS="$OPTS --default-library=$DEF_LIB"
meson build --werror -Dexamples=all ${OPTS}
ninja -C build

