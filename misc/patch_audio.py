#!/usr/bin/env python3

import sys, ctypes, hashlib
from pathlib import Path
from lz4.block import decompress
import capstone as cs


class NsoHeader(ctypes.Structure):
    TextCompress   = 1 << 0
    RodataCompress = 1 << 1
    DataCompress   = 1 << 2
    TextHash       = 1 << 3
    RodataHash     = 1 << 4
    DataHash       = 1 << 5

    class SegmentHeader(ctypes.Structure):
        _fields_ = [
            ("file_off",    ctypes.c_uint32),
            ("memory_off",  ctypes.c_uint32),
            ("size",        ctypes.c_uint32),
        ]

    class SegmentHeaderRelative(ctypes.Structure):
        _fields_ = [
            ("offset",      ctypes.c_uint32),
            ("size",        ctypes.c_uint32),
        ]

    _fields_ = [
        ("magic",           ctypes.c_uint32),
        ("version",         ctypes.c_uint32),
        ("reserved1",       ctypes.c_uint32),
        ("flags",           ctypes.c_uint32),
        ("text_hdr",        SegmentHeader),
        ("module_name_off", ctypes.c_uint32),
        ("rodata_hdr",      SegmentHeader),
        ("module_name_sz",  ctypes.c_uint32),
        ("data_hdr",        SegmentHeader),
        ("bss_sz",          ctypes.c_uint32),
        ("build_id",        ctypes.c_uint8 * 0x20),
        ("text_comp_sz",    ctypes.c_uint32),
        ("rodata_comp_sz",  ctypes.c_uint32),
        ("data_comp_sz",    ctypes.c_uint32),
        ("reserved2",       ctypes.c_uint8 * 0x1c),
        ("api_info_hdr",    SegmentHeaderRelative),
        ("dynstr_info_hdr", SegmentHeaderRelative),
        ("dynsym_info_hdr", SegmentHeaderRelative),
        ("text_hash",       ctypes.c_uint8 * 0x20),
        ("rodata_hash",     ctypes.c_uint8 * 0x20),
        ("data_hash",       ctypes.c_uint8 * 0x20),
    ]

assert(ctypes.sizeof(NsoHeader) == 0x100)


def find_clamp(text):
    d        = cs.Cs(cs.CS_ARCH_ARM64, cs.CS_MODE_ARM | cs.CS_MODE_LITTLE_ENDIAN)
    d.detail = True

    # Expected instruction sequence:
    # (this is a common pattern, so we narrow it down using the stack offset, and assert unicity of the result)
    # fmov      s1,1.0
    # ldrb      w8,[x0, #0x5]
    # mov       x19,x0
    # fcmp      s0,s1
    # fcsel     s1,s1,s0,gt
    # fcmp      s0,#0.0
    # movi      d0,#0x0
    # fcsel     s1,s0,s1,mi

    offsets = []
    state = val = 0
    for i in range(0, len(text), 4):
        try:
            inst = next(d.disasm(text[i:i+4], 0x7100000000))
        except StopIteration:
            continue

        if inst.group in (cs.arm64.ARM64_GRP_JUMP, cs.arm64.ARM64_GRP_CALL, cs.arm64.ARM64_GRP_RET):
            state = 0

        match len(inst.operands):
            case 2:
                reg0, reg1 = inst.operands[0], inst.operands[1]
            case 3:
                reg0, reg1, reg2 = inst.operands[0], inst.operands[1], inst.operands[2]

        match state:
            case 0:
                if inst.id == cs.arm64.ARM64_INS_ADD and \
                        reg0.type == cs.arm64.ARM64_OP_REG and \
                        reg1.type == cs.arm64.ARM64_OP_REG and reg1.value.reg == cs.arm64.ARM64_REG_SP and \
                        reg2.type == cs.arm64.ARM64_OP_IMM and reg2.value.imm == 8:
                    state = 1
            case 1:
                if inst.id == cs.arm64.ARM64_INS_FMOV:
                    state = 2 if reg0.type == cs.arm64.ARM64_OP_REG and reg1.type == cs.arm64.ARM64_OP_FP and reg1.value.fp == 1.0 \
                              else 0
                    val = reg0.value.reg
            case 2:
                if inst.id == cs.arm64.ARM64_INS_FCMP:
                    state = 3 if reg0.type == cs.arm64.ARM64_OP_REG and reg0.value.reg in (cs.arm64.ARM64_REG_S0, val) and \
                                 reg1.type == cs.arm64.ARM64_OP_REG and reg1.value.reg in (cs.arm64.ARM64_REG_S0, val) \
                              else 0
            case 3:
                if inst.id == cs.arm64.ARM64_INS_FCSEL and \
                        reg1.type == cs.arm64.ARM64_OP_REG and reg1.value.reg == val and \
                        reg2.type == cs.arm64.ARM64_OP_REG and reg2.value.reg == cs.arm64.ARM64_REG_S0 and \
                        inst.cc == cs.arm64.ARM64_CC_GT:
                    offsets.append(i)
                    state = 0

    return offsets[0] if len(offsets) == 1 else None


def main(argc, argv):
    if argc != 2:
        print(f"Usage: {argv[0]} nso")
        return

    print(f"Patching {argv[1]}")

    p = Path(argv[1])
    fp = p.open("rb")

    hdr = NsoHeader()
    fp.readinto(hdr)

    build_id = bytes(hdr.build_id).rstrip(b"\0").hex()
    print(f"Build id: {build_id}")

    if hdr.module_name_sz > 1:
        fp.seek(hdr.module_name_off)
        print(f"Module name: {fp.read(hdr.module_name_sz).decode().strip('\0')}")

    fp.seek(hdr.text_hdr.file_off)
    text = fp.read(hdr.text_comp_sz)

    if hdr.flags & NsoHeader.TextCompress:
        text = decompress(text, uncompressed_size=hdr.text_hdr.size)
        assert(len(text) == hdr.text_hdr.size)

    if hdr.flags & NsoHeader.TextHash:
        h = hashlib.sha256(text).digest()
        assert(h == bytes(hdr.text_hash))

    off = find_clamp(text)
    if off is None:
        print("Failed to find clamp instruction")
        return

    seq = b"\x01\x40\x20\x1e" # fmov s1,s0

    patch = b"PATCH"
    patch += (off + ctypes.sizeof(NsoHeader)).to_bytes(3, byteorder="big")
    patch += len(seq).to_bytes(2, byteorder="big")
    patch += seq
    patch += b"EOF"

    dest = p.parent / (build_id + ".ips")
    with open(dest, "wb") as out:
        out.write(patch)


if __name__ == "__main__":
    main(len(sys.argv), sys.argv)
