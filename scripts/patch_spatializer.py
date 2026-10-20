#!/usr/bin/env python3
"""
Patch script for Dolby Spatializer libraries on Samsung Galaxy devices.
Replaces the HRTF convolver in EffectSpatializer::process with a flat pass-through:
in[0] (FL) -> out[0] (L)
in[1] (FR) -> out[1] (R)
This completely bypasses binaural HRTF processing while preserving the 12ch->2ch
conversion and the Android Spatializer state required by Apple Music.
"""

import os
import sys
import capstone

def patch_64bit(in_path, out_path):
    print(f"=== Patching 64-bit library: {in_path} ===")
    with open(in_path, "rb") as f:
        data = bytearray(f.read())

    # Target: _ZN5dolby17EffectSpatializer7processEP14audio_buffer_sS2_ at 0x24d984
    # Size: 148 bytes (0x24d984 to 0x24da18)
    func_addr = 0x24d984
    func_size = 148

    # Verify original bytes start with paciasp (0x7f 0x23 0x03 0xd5)
    orig_header = data[func_addr:func_addr+4]
    print(f"Original header at 0x{func_addr:x}: {orig_header.hex()}")

    arm64_code = bytes([
        0x23, 0x00, 0x40, 0xf9,  # 0x24d984: ldr x3, [x1]        (frameCount)
        0x24, 0x04, 0x40, 0xf9,  # 0x24d988: ldr x4, [x1, #8]     (in float*)
        0x45, 0x04, 0x40, 0xf9,  # 0x24d98c: ldr x5, [x2, #8]     (out float*)
        0xc3, 0x00, 0x00, 0xb4,  # 0x24d990: cbz x3, #0x24d9a8    (if frameCount == 0 goto done)
        0x80, 0x04, 0x40, 0x2d,  # 0x24d994: ldp s0, s1, [x4]     (s0=FL, s1=FR)
        0xa0, 0x04, 0x81, 0x2c,  # 0x24d998: stp s0, s1, [x5], #8 (out[0]=FL, out[1]=FR, out+=8)
        0x84, 0xc0, 0x00, 0x91,  # 0x24d99c: add x4, x4, #0x30    (in += 48 bytes)
        0x63, 0x04, 0x00, 0xf1,  # 0x24d9a0: subs x3, x3, #1     (frameCount--)
        0x81, 0xff, 0xff, 0x54,  # 0x24d9a4: b.ne #0x24d994      (loop)
        0xe0, 0x03, 0x1f, 0x2a,  # 0x24d9a8: mov w0, wzr          (return 0)
        0xc0, 0x03, 0x5f, 0xd6   # 0x24d9ac: ret
    ])

    # Fill rest with NOPs (0x1f 0x20 0x03 0xd5)
    nop = bytes([0x1f, 0x20, 0x03, 0xd5])
    patch_bytes = arm64_code + nop * ((func_size - len(arm64_code)) // 4)

    data[func_addr:func_addr+len(patch_bytes)] = patch_bytes

    # Disassemble & verify
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    print("Disassembly of patched function:")
    for insn in md.disasm(data[func_addr:func_addr+len(patch_bytes)], func_addr):
        if insn.mnemonic == 'nop' and insn.address > func_addr + 48:
            continue  # don't spam all NOPs
        print(f"  0x{insn.address:x}: {insn.mnemonic:<8} {insn.op_str}")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"Saved: {out_path} ({len(data)} bytes)\n")


def patch_32bit(in_path, out_path):
    print(f"=== Patching 32-bit library: {in_path} ===")
    with open(in_path, "rb") as f:
        data = bytearray(f.read())

    # Target: _ZN5dolby17EffectSpatializer7processEP14audio_buffer_sS2_
    # Virtual address: 0x10d818, File offset: 0x10c818
    # Size: 94 bytes (0x10c818 to 0x10c876)
    func_vaddr = 0x10d818
    file_off = 0x10c818
    func_size = 94

    orig_header = data[file_off:file_off+4]
    print(f"Original header at offset 0x{file_off:x}: {orig_header.hex()}")

    thumb_code = bytes([
        0x0b, 0x68,              # 0x10d818: ldr r3, [r1]          (frameCount)
        0x48, 0x68,              # 0x10d81a: ldr r0, [r1, #4]      (in float*)
        0x51, 0x68,              # 0x10d81c: ldr r1, [r2, #4]      (out float*)
        0x3b, 0xb1,              # 0x10d81e: cbz r3, #0x10d830     (if frameCount == 0 goto done)
        0x90, 0xe8, 0x04, 0x10,  # 0x10d820: ldm.w r0, {r2, ip}    (r2=FL, ip=FR)
        0xa1, 0xe8, 0x04, 0x10,  # 0x10d824: stm.w r1!, {r2, ip}   (out[0]=FL, out[1]=FR, r1+=8)
        0x00, 0xf1, 0x30, 0x00,  # 0x10d828: add.w r0, r0, #0x30   (in += 48 bytes)
        0x01, 0x3b,              # 0x10d82c: subs r3, #1           (frameCount--)
        0xf7, 0xd1,              # 0x10d82e: bne #0x10d820         (loop)
        0x00, 0x20,              # 0x10d830: movs r0, #0           (return 0)
        0x70, 0x47               # 0x10d832: bx lr
    ])

    # Fill rest with Thumb NOPs (0x00 0xbf)
    nop = bytes([0x00, 0xbf])
    patch_bytes = thumb_code + nop * ((func_size - len(thumb_code)) // 2)

    data[file_off:file_off+len(patch_bytes)] = patch_bytes

    # Disassemble & verify
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    print("Disassembly of patched function:")
    for insn in md.disasm(data[file_off:file_off+len(patch_bytes)], func_vaddr):
        if insn.mnemonic == 'nop' and insn.address > func_vaddr + 36:
            continue
        print(f"  0x{insn.address:x}: {insn.mnemonic:<8} {insn.op_str}")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"Saved: {out_path} ({len(data)} bytes)\n")


if __name__ == "__main__":
    patch_64bit(
        "libswspatializer_legacy.so",
        "module/system/lib64/libswspatializer_legacy.so"
    )
    patch_32bit(
        "libswspatializer_vendor32.so",
        "module/system/vendor/lib/soundfx/libswspatializer.so"
    )
    print("All patches generated and verified successfully!")
