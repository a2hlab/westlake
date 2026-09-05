"""Apply only the optional-extension assertion removal to the pinned binary.

Paired with the compiled EGL boundary's real KHR/EXT/core-swap dispatch. This
keeps every unrelated HWUI instruction intact; never apply without that DSO.
"""
from pathlib import Path
import hashlib,struct
ROOT=Path(__file__).resolve().parent
def build(output):
    data=bytearray((ROOT/'baseline-libhwui.so').read_bytes())
    assert hashlib.sha256(data).hexdigest()=='a11487ce8bfe75498f48bf64edda4fbed582b5dc4ec51f695fb8d6403a820c86'
    assert data[:6]==b'\x7fELF\x02\x01'
    phoff=struct.unpack_from('<Q',data,32)[0]
    size,count=struct.unpack_from('<HH',data,54)
    patches={
        # TBZ w0,0,fatal_required_extension -> NOP. Other errors remain fatal.
        0x187674:('200b0036','1f2003d5'),
        # The pinned HWUI has a local swap wrapper, resolving directly from
        # libEGL and bypassing symbol interposition. Resolve only its damage
        # target from RTLD_DEFAULT (mov x0,x19 -> mov x0,xzr), selecting our
        # first-preloaded guarded boundary. That boundary uses a real libEGL
        # handle itself, so it never resolves back to this local wrapper.
        0x12c6e0:('e00313aa','e0031faa'),
    }
    for address,(before,after) in patches.items():
        offset=None
        for i in range(count):
            kind,flags,off,va,pa,filesz,memsz,align=struct.unpack_from('<II6Q',data,phoff+i*size)
            if kind==1 and va<=address<va+filesz:
                offset=off+address-va;assert flags&1;break
        assert offset is not None and data[offset:offset+4]==bytes.fromhex(before)
        data[offset:offset+4]=bytes.fromhex(after)
    output.write_bytes(data)
if __name__=='__main__':build(ROOT/'libhwui-optional-egl.so')
