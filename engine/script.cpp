#include "script.h"

#include <stdexcept>
#include <string>

namespace ut {

namespace {

struct Walker {
    const Package& p;
    Reader r;
    uint64_t mem = 0;

    uint8_t byte() {
        mem += 1;
        return r.u8();
    }
    void raw(size_t n) {
        r.need(n);
        r.pos += n;
        mem += n;
    }
    uint16_t u16() {
        mem += 2;
        return r.u16();
    }
    int32_t ref() {  // compact on disk, 4 bytes in memory
        mem += 4;
        return r.index();
    }
    void params() {
        while (expr() != 0x16) {
        }
    }
    uint8_t expr() {
        uint8_t t = byte();
        switch (t) {
            case 0x00: case 0x01: case 0x02: case 0x20: case 0x21: case 0x29: ref(); break;
            case 0x04: case 0x0D: case 0x0E: case 0x2D: expr(); break;
            case 0x05: raw(1); expr(); break;
            case 0x06: u16(); break;
            case 0x07: case 0x09: case 0x18: u16(); expr(); break;
            case 0x0A: if (u16() != 0xFFFF) expr(); break;
            case 0x0C:
                for (;;) {
                    int32_t name = ref();
                    raw(4);
                    if (name < 0 || size_t(name) >= p.names.size()) throw std::runtime_error("bad label name");
                    if (p.names[size_t(name)] == "None") break;
                }
                break;
            case 0x0F: case 0x10: case 0x14: case 0x1A: expr(); expr(); break;
            case 0x11: for (int i = 0; i < 4; i++) expr(); break;
            case 0x12: case 0x19: expr(); u16(); raw(1); expr(); break;
            case 0x13: case 0x2E: case 0x36: ref(); expr(); break;
            case 0x1B: case 0x38: case 0x1C: ref(); params(); break;
            case 0x1D: case 0x1E: raw(4); break;
            case 0x1F: while (byte()) {} break;
            case 0x34: while (u16()) {} break;
            case 0x22: case 0x23: raw(12); break;
            case 0x24: case 0x2C: raw(1); break;
            case 0x2F: expr(); u16(); break;
            case 0x32: case 0x33: ref(); expr(); expr(); break;
            case 0x08: case 0x0B: case 0x16: case 0x17: case 0x25: case 0x26: case 0x27: case 0x28: case 0x2A: case 0x30: case 0x31: break;
            default:
                if (t >= 0x39 && t < 0x60) expr();
                else if (t >= 0x60 && t < 0x70) raw(1), params();
                else if (t >= 0x70) params();
                else throw std::runtime_error("unknown bytecode token " + std::to_string(t));
        }
        return t;
    }
};

}  // namespace

size_t skip_script(const Package& p, size_t pos, uint32_t mem_size) {
    Walker w{p, Reader(p.data, pos)};
    while (w.mem < mem_size) w.expr();
    if (w.mem != mem_size) throw std::runtime_error("script does not end at its declared size");
    return w.r.pos;
}

ClassInfo read_class(const Package& p, const Export& cls) {
    Reader r(p.data, size_t(cls.offset));
    ClassInfo c;
    c.super_ref = r.index();
    for (int i = 0; i < 4; i++) r.index();  // next, script text, children, friendly name
    r.i32(), r.i32();                        // line, text pos
    uint32_t size = r.u32();
    r.pos = skip_script(p, r.pos, size);
    r.need(22 + 4 + 16);
    r.pos += 22 + 4 + 16;  // state fields, class flags, guid
    for (int32_t n = r.index(); n-- > 0;) r.index(), r.u32(), r.u32();  // dependencies
    for (int32_t n = r.index(); n-- > 0;) r.index();                   // package imports
    r.index(), r.index();                                              // within, config name
    Export defaults = cls;
    defaults.offset = int32_t(r.pos);
    defaults.size = cls.offset + cls.size - int32_t(r.pos);
    defaults.flags = 0;
    size_t end = 0;
    c.defaults = read_properties(p, defaults, &end);
    if (end != size_t(cls.offset) + size_t(cls.size)) throw std::runtime_error("class data size mismatch");
    return c;
}

}  // namespace ut
