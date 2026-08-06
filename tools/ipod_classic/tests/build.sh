#!/bin/sh
# Build the bare-metal tests for the iPod-Classic machine.
#
# Needs an ARM bare-metal toolchain:
#   sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi
#
# Output goes to assets/tests/, which is gitignored along with the rest of
# assets/.

set -e

here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/../../.." && pwd)/assets/tests
mkdir -p "$out"

CC=${CC:-arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}

for src in "$here"/*.c; do
    name=$(basename "$src" .c)
    "$CC" -mcpu=arm926ej-s -marm -nostdlib -nostartfiles -ffreestanding \
          -O1 -g -Wall -Wextra \
          -T "$here/test.lds" "$here/start.S" "$src" \
          -o "$out/$name.elf"
    "$OBJCOPY" -O binary "$out/$name.elf" "$out/$name.bin"
    echo "built $out/$name.bin"
done
