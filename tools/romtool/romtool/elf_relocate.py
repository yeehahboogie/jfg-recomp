"""Generic relocation of a linked module image to new load addresses, the way N64Recomp's runtime
macros (RELOC_HI16/LO16, relocated jal) and the game's own data relocation behave."""
import struct

from .build import Program, section_name


def relocate_module(program: Program, module_index: int, bases: dict[int, int]) -> bytes:
    name = section_name(module_index)
    section = next(s for s in program.sections if s.name == name)
    image = bytearray(section.data)
    delta = {section_name(i): bases[i] - link for i, link in program.link_base.items()}
    rels = [r for r in program.rels if r.section == name]

    def word(off: int) -> int:
        return struct.unpack_from(">I", image, off)[0]

    i = 0
    while i < len(rels):
        r = rels[i]
        off, d = r.vram - section.vram, delta[r.target]
        w = word(off)
        if r.type == 2:
            struct.pack_into(">I", image, off, (w + d) & 0xFFFFFFFF)
        elif r.type == 4:
            struct.pack_into(">I", image, off, (w & 0xFC000000) | (((w & 0x3FFFFFF) + (d >> 2)) & 0x3FFFFFF))
        elif r.type == 5:
            lo_off = rels[i + 1].vram - section.vram
            lo = word(lo_off)
            full = (((w & 0xFFFF) << 16) + (((lo & 0xFFFF) ^ 0x8000) - 0x8000) + d) & 0xFFFFFFFF
            struct.pack_into(">H", image, off + 2, ((full >> 16) + ((full >> 15) & 1)) & 0xFFFF)
            struct.pack_into(">H", image, lo_off + 2, full & 0xFFFF)
            i += 2
            continue
        elif r.type == 6:
            struct.pack_into(">H", image, off + 2, ((w & 0xFFFF) + d) & 0xFFFF)
        i += 1
    return bytes(image)
