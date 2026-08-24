#!/usr/bin/env python3
"""Generate the checked-in ELF64 ET_REL fixtures for tests/ffi.

No hosted toolchain is involved: the objects are encoded byte by byte so the
luna-link ELF reader can be exercised on any host. Run this script to
regenerate the .o files next to it; the results are committed.

Fixtures:
  answer.o     valid object exporting ffi_answer/ffi_answer_plus, with local
               symbols, PC32/PLT32/64 relocations and a dropped
               .note.GNU-stack section
  missing.o    valid object that defines nothing global (undefined-symbol
               link failure)
  bad_class.o  answer.o with EI_CLASS corrupted to ELFCLASS32
  bad_reloc.o  answer.o with an unsupported R_X86_64_GOT32 entry
  truncated.o  answer.o cut short inside the section table
"""

from __future__ import annotations

import pathlib
import struct

# section types
SHT_NULL = 0
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOBITS = 8

# section flags
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

# symbol bindings and types
STB_LOCAL = 0
STB_GLOBAL = 1
STT_OBJECT = 1
STT_FUNC = 2
STT_SECTION = 3

# relocation types
R_X86_64_64 = 1
R_X86_64_PC32 = 2
R_X86_64_GOT32 = 3
R_X86_64_PLT32 = 4

HEADER_SIZE = 64
SECTION_SIZE = 64
SYMBOL_SIZE = 24
RELA_SIZE = 24


def symbol(name: int, info: int, shndx: int, value: int, size: int) -> bytes:
    return struct.pack("<IBBHQQ", name, info, 0, shndx, value, size)


def rela(offset: int, sym: int, kind: int, addend: int) -> bytes:
    return struct.pack("<QQq", offset, (sym << 32) | kind, addend)


def build_object(
    text: bytes,
    data: bytes,
    rela_text: bytes,
    rela_data: bytes,
    symbols: list[bytes],
    first_global: int,
    strtab: bytes,
) -> bytes:
    names = b"\0"
    name_offsets: dict[str, int] = {}
    for section_name in (
        ".text",
        ".data",
        ".rela.text",
        ".rela.data",
        ".symtab",
        ".strtab",
        ".shstrtab",
        ".note.GNU-stack",
    ):
        name_offsets[section_name] = len(names)
        names += section_name.encode() + b"\0"

    # index, name, type, flags, align, content, link, info, entsize
    sections = [
        (0, SHT_NULL, 0, 0, b"", 0, 0, 0),
        (".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16, text, 0, 0, 0),
        (".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8, data, 0, 0, 0),
        (".rela.text", SHT_RELA, 0, 8, rela_text, 5, 1, RELA_SIZE),
        (".rela.data", SHT_RELA, 0, 8, rela_data, 5, 2, RELA_SIZE),
        (".symtab", SHT_SYMTAB, 0, 8, b"".join(symbols), 6, first_global, SYMBOL_SIZE),
        (".strtab", SHT_STRTAB, 0, 1, strtab, 0, 0, 0),
        (".shstrtab", SHT_STRTAB, 0, 1, names, 0, 0, 0),
        (".note.GNU-stack", SHT_PROGBITS, 0, 1, b"", 0, 0, 0),
    ]

    image = bytearray(b"\0" * HEADER_SIZE)
    headers = bytearray()
    for name, kind, flags, align, content, link, info, entsize in sections:
        if kind == SHT_NULL:
            headers += b"\0" * SECTION_SIZE
            continue
        while len(image) % align:
            image += b"\0"
        offset = len(image)
        image += content
        headers += struct.pack(
            "<IIQQQQIIQQ",
            name_offsets[name],
            kind,
            flags,
            0,
            offset,
            len(content),
            link,
            info,
            align,
            entsize,
        )
    while len(image) % 8:
        image += b"\0"
    section_offset = len(image)
    image += headers

    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\0" * 8
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        ident,
        1,  # ET_REL
        62,  # EM_X86_64
        1,
        0,
        0,
        section_offset,
        0,
        HEADER_SIZE,
        56,
        0,
        SECTION_SIZE,
        len(sections),
        7,
    )
    image[0:HEADER_SIZE] = header
    return bytes(image)


def build_answer() -> bytes:
    # add7 (local):          lea 7(%rdi), %eax; ret
    # ffi_answer:            mov lucky_ptr(%rip), %rax; mov (%rax), %eax; ret
    # ffi_answer_plus:       sub $8, %rsp; call add7; add $8, %rsp; ret
    text = (
        b"\x8d\x47\x07\xc3"
        b"\x48\x8b\x05\x00\x00\x00\x00\x8b\x00\xc3"
        b"\x48\x83\xec\x08\xe8\x00\x00\x00\x00\x48\x83\xc4\x08\xc3"
    )
    assert len(text) == 28
    # lucky (local): .long 42; pad; lucky_ptr (local): .quad lucky
    data = b"\x2a\x00\x00\x00" + b"\0" * 12
    assert len(data) == 16
    strtab = b"\0add7\0lucky\0lucky_ptr\0ffi_answer\0ffi_answer_plus\0"
    names = {
        "add7": 1,
        "lucky": 6,
        "lucky_ptr": 12,
        "ffi_answer": 22,
        "ffi_answer_plus": 33,
    }
    for name, offset in names.items():
        assert strtab[offset : offset + len(name)] == name.encode()
    local_section = (STB_LOCAL << 4) | STT_SECTION
    local_func = (STB_LOCAL << 4) | STT_FUNC
    local_object = (STB_LOCAL << 4) | STT_OBJECT
    global_func = (STB_GLOBAL << 4) | STT_FUNC
    symbols = [
        symbol(0, 0, 0, 0, 0),
        symbol(0, local_section, 1, 0, 0),
        symbol(0, local_section, 2, 0, 0),
        symbol(names["add7"], local_func, 1, 0, 4),
        symbol(names["lucky"], local_object, 2, 0, 4),
        symbol(names["lucky_ptr"], local_object, 2, 8, 8),
        symbol(names["ffi_answer"], global_func, 1, 4, 10),
        symbol(names["ffi_answer_plus"], global_func, 1, 14, 14),
    ]
    rela_text = rela(7, 5, R_X86_64_PC32, -4) + rela(19, 3, R_X86_64_PLT32, -4)
    rela_data = rela(8, 4, R_X86_64_64, 0)
    return build_object(text, data, rela_text, rela_data, symbols, 6, strtab)


def build_missing() -> bytes:
    # one local function, nothing global: linking against a reference to
    # ffi_answer must fail with an undefined symbol.
    text = b"\xb8\x01\x00\x00\x00\xc3"
    strtab = b"\0local_only\0"
    symbols = [
        symbol(0, 0, 0, 0, 0),
        symbol(0, (STB_LOCAL << 4) | STT_SECTION, 1, 0, 0),
        symbol(1, (STB_LOCAL << 4) | STT_FUNC, 1, 0, len(text)),
    ]
    return build_object(text, b"", b"", b"", symbols, 3, strtab)


def check_structure(image: bytes) -> None:
    assert image[:6] == b"\x7fELF\x02\x01"
    section_offset = struct.unpack_from("<Q", image, 40)[0]
    section_count = struct.unpack_from("<H", image, 60)[0]
    assert section_offset + section_count * SECTION_SIZE == len(image)


def main() -> None:
    here = pathlib.Path(__file__).resolve().parent
    answer = build_answer()
    check_structure(answer)

    bad_class = bytearray(answer)
    bad_class[4] = 1  # ELFCLASS32

    bad_reloc = bytearray(answer)
    section_offset = struct.unpack_from("<Q", answer, 40)[0]
    for index in range(struct.unpack_from("<H", answer, 60)[0]):
        base = section_offset + index * SECTION_SIZE
        kind, _, _, offset, size = struct.unpack_from("<IQQQQ", answer, base + 4)
        if kind == SHT_RELA and size:
            struct.pack_into("<Q", bad_reloc, offset + 8, (5 << 32) | R_X86_64_GOT32)
            break
    else:
        raise AssertionError("no RELA section found in answer.o")

    fixtures = {
        "answer.o": answer,
        "missing.o": build_missing(),
        "bad_class.o": bytes(bad_class),
        "bad_reloc.o": bytes(bad_reloc),
        "truncated.o": answer[: len(answer) - 2 * SECTION_SIZE],
    }
    for name, content in fixtures.items():
        (here / name).write_bytes(content)
        print(f"  wrote {name} ({len(content)} bytes)")


if __name__ == "__main__":
    main()
