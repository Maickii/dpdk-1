#!/bin/bash -xe

function on_error() {
    FILES_TO_PRINT=( "build/meson-logs/testlog.txt" "build/.ninja_log" "build/meson-logs/meson-log.txt")

    for pr_file in "${FILES_TO_PRINT[@]}"; do
        if [ -e "$pr_file" ]; then
            cat "$pr_file"
        fi
    done
}
trap on_error ERR

if [ "${AARCH64}" == "1" ]; then
    # convert the arch specifier
    OPTS="${OPTS} -DRTE_ARCH_64=1 --cross-file config/arm/arm64_armv8_linuxapp_gcc"
fi

OPTS="$OPTS --default-library=$DEF_LIB"
meson build --werror -Dexamples=all ${OPTS}
ninja -C build
