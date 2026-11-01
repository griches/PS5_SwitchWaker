#!/usr/bin/env bash
# Build only the CRT and import stubs; Ninja owns their dependencies.
# SPDX-License-Identifier: MIT
set -euo pipefail
work=$1 vulkan=$2 sdk=$3
native="$vulkan/tooling/native"
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
mkdir -p "$work/link/obj" "$work/link/stubs"
cc() {
    PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" \
        "-ffile-prefix-map=$vulkan=src/PS5_Vulkan" \
        "-ffile-prefix-map=$(realpath "$vulkan")=src/PS5_Vulkan" \
        "-ffile-prefix-map=$sdk=sdk" "-ffile-prefix-map=$(realpath "$sdk")=sdk" \
        "-ffile-prefix-map=$work=build" -fdebug-compilation-dir=. "$@"
}

# The CRT, as in
# PS5_Vulkan's CTS title.
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -MD -MF "$work/link/obj/app_crt.d" -c "$native/app_crt.cpp" -o "$work/link/obj/app_crt.o"
# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/link/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/link/stubs/${library}.so" "$work/link/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c
