#!/usr/bin/env python3
"""Emit a tiny raw ARM binary that greets us over UART0, then spins.

Used to smoke-test the iPod-Classic machine before any real firmware exists:
it proves the CPU starts at SDRAM_MEM_BASE, executes ARM code, and that the
serial console is wired to the right address.

    python mktestbin.py hello.bin
    qemu-system-arm -M iPod-Classic -kernel hello.bin -serial mon:stdio -d unimp

Hand-assembled, so there is no toolchain dependency.
"""

import struct
import sys

UART0_BASE = 0x3CC00000
UTXH = 0x20          # exynos4210 UART transmit holding register
MESSAGE = "S5L8702 alive\r\n"


def emit(message=MESSAGE, uart_base=UART0_BASE):
    code = []

    # r0 = uart_base, loaded from a literal placed right after the code.
    # The literal offset is patched once we know how long the body is.
    code.append(("ldr_r0_pc", None))

    for ch in message:
        # mov r1, #<char>   -- ASCII fits in the 8-bit immediate field
        code.append(("word", 0xE3A01000 | (ord(ch) & 0xFF)))
        # str r1, [r0, #UTXH]
        code.append(("word", 0xE5801000 | UTXH))

    code.append(("word", 0xEAFFFFFE))  # b .

    n = len(code)
    literal_at = n * 4
    # PC reads as the instruction address + 8 on ARM.
    disp = literal_at - (0 + 8)
    assert 0 <= disp < 0x1000, "literal pool out of ldr range"

    out = bytearray()
    for kind, val in code:
        if kind == "ldr_r0_pc":
            out += struct.pack("<I", 0xE59F0000 | disp)
        else:
            out += struct.pack("<I", val)
    out += struct.pack("<I", uart_base)
    return bytes(out)


def main(argv):
    path = argv[1] if len(argv) > 1 else "hello.bin"
    data = emit()
    with open(path, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes, %d instructions)" %
          (path, len(data), len(data) // 4 - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
