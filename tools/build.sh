#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
llvm=${LLVM_BIN:-/opt/homebrew/opt/llvm@18/bin}
sdk="$root/build/deps/PS5_Vulkan/.deps/native/ps5-payload-sdk"
python3 tools/gen-vulkan-shim.py build/deps/PS5_Mesa/include/vulkan/vulkan_core.h build/vulkan_shim.c
cmake -S ps5 -B build/ps5 -G Ninja \
 -DCMAKE_TOOLCHAIN_FILE="$root/ps5/cmake/ps5.cmake" \
 -DCMAKE_C_COMPILER="$llvm/clang" -DCMAKE_CXX_COMPILER="$llvm/clang++" \
 -DPS5_PAYLOAD_SDK="$sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build build/ps5 -j"${JOBS:-8}"
docker image inspect reundr2-ps5-builder:local >/dev/null 2>&1 || docker build -t reundr2-ps5-builder:local -f ps5/Dockerfile ps5
# Same absolute host path inside Docker also resolves CMake's object paths.
docker run --rm -v "$(dirname "$root"):$(dirname "$root")" -w "$root" reundr2-ps5-builder:local bash tools/link.sh
n=build/deps/PS5_Vulkan/tooling/native
"$llvm/clang++" -std=c++20 -O2 "$n/native_app_builder.cpp" "$n/self_container.cpp" "$n/elf_object.cpp" "$n/sce_module_writer.cpp" -lz -o build/ps5-native-tool
