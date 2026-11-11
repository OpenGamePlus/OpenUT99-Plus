#include "vm.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <cstring>

#include "script.h"

namespace ut {

namespace {

const int kMaxCallDepth = 250;

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

bool iequal(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

// Character operations count code points, so strings are decoded from UTF-8 for them.
std::u32string decode(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        auto c = uint8_t(s[i]);
        int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        char32_t cp = n == 0 ? c : char32_t(c & (0x3F >> n));
        size_t j = 1;
        for (; j <= size_t(n) && i + j < s.size(); j++) cp = cp << 6 | char32_t(uint8_t(s[i + j]) & 0x3F);
        out.push_back(cp);
        i += j;
    }
    return out;
}

std::string encode(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | c >> 6), out += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += char(0xE0 | c >> 12), out += char(0x80 | (c >> 6 & 0x3F)), out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | c >> 18), out += char(0x80 | (c >> 12 & 0x3F)), out += char(0x80 | (c >> 6 & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string substr(const std::string& s, int32_t start, int32_t count) {  // clamped, in code points
    std::u32string u = decode(s);
    int64_t n = int64_t(u.size());
    int64_t b = start, e = int64_t(start) + count;
    if (b < 0) b = 0;
    if (e > n) e = n;
    return b >= e ? std::string() : encode(u.substr(size_t(b), size_t(e - b)));
}

// Integer arithmetic wraps like 32-bit hardware instead of being undefined.
int32_t wrap(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }

int32_t to_int(float f) {  // shortcut: out-of-range and NaN give 0, unverified against the original
    return f > -2147483648.f && f < 2147483648.f ? int32_t(f) : 0;
}

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

const float kRotToRad = float(M_PI / 32768.0);

VT type_of(const std::string& prop_class, const std::string& struct_name) {
    if (prop_class == "ByteProperty") return VT::Byte;
    if (prop_class == "IntProperty") return VT::Int;
    if (prop_class == "BoolProperty") return VT::Bool;
    if (prop_class == "FloatProperty") return VT::Float;
    if (prop_class == "NameProperty") return VT::Name;
    if (prop_class == "StrProperty") return VT::Str;
    if (prop_class == "ObjectProperty" || prop_class == "ClassProperty") return VT::Object;
    if (prop_class == "StructProperty" && struct_name == "Vector") return VT::Vector;
    if (prop_class == "StructProperty" && struct_name == "Rotator") return VT::Rotator;
    return VT::None;  // other structs, arrays, maps: not supported yet
}

bool equal(const Value& a, const Value& b) {
    switch (a.t) {
        case VT::Float: return a.f == b.f;
        case VT::Str: return a.s == b.s;
        case VT::Name: return iequal(a.s, b.s);
        case VT::Vector: return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2];
        case VT::Rotator: return a.r[0] == b.r[0] && a.r[1] == b.r[1] && a.r[2] == b.r[2];
        default: return a.i == b.i;
    }
}

void assign(Value& dst, const Value& src) {  // keeps the declared type of dst
    VT t = dst.t;
    dst = src;
    if (t != VT::None) dst.t = t;
}

Value make(VT t, int32_t i) {
    Value v = Value::of(t);
    v.i = i;
    return v;
}

Value str(std::string s) {
    Value v = Value::of(VT::Str);
    v.s = std::move(s);
    return v;
}

Value convert(uint8_t t, const Value& a) {  // tokens 0x39..0x59, names from public UE1 documentation
    switch (t) {
        case 0x39: {  // RotatorToVector
            float p = float(a.r[0]) * kRotToRad, y = float(a.r[1]) * kRotToRad;
            Value v = Value::of(VT::Vector);
            v.v[0] = std::cos(p) * std::cos(y), v.v[1] = std::cos(p) * std::sin(y), v.v[2] = std::sin(p);
            return v;
        }
        case 0x3A: return make(VT::Int, a.i);                 // ByteToInt
        case 0x3B: return make(VT::Bool, a.i != 0);           // ByteToBool
        case 0x3C: { Value v = Value::of(VT::Float); v.f = float(a.i); return v; }  // ByteToFloat
        case 0x3D: return make(VT::Byte, a.i & 0xFF);         // IntToByte
        case 0x3E: return make(VT::Bool, a.i != 0);           // IntToBool
        case 0x3F: { Value v = Value::of(VT::Float); v.f = float(a.i); return v; }  // IntToFloat
        case 0x40: return make(VT::Byte, a.i != 0);           // BoolToByte
        case 0x41: return make(VT::Int, a.i != 0);            // BoolToInt
        case 0x42: { Value v = Value::of(VT::Float); v.f = a.i ? 1.f : 0.f; return v; }  // BoolToFloat
        case 0x43: return make(VT::Byte, to_int(a.f) & 0xFF);  // FloatToByte
        case 0x44: return make(VT::Int, to_int(a.f));          // FloatToInt
        case 0x45: return make(VT::Bool, a.f != 0);            // FloatToBool
        case 0x47: return make(VT::Bool, a.i != 0);            // ObjectToBool
        case 0x48: return make(VT::Bool, !iequal(a.s, "None"));  // NameToBool
        case 0x49: return make(VT::Byte, int32_t(std::strtol(a.s.c_str(), nullptr, 10)) & 0xFF);  // StringToByte
        case 0x4A: return make(VT::Int, int32_t(std::strtol(a.s.c_str(), nullptr, 10)));          // StringToInt
        case 0x4B: return make(VT::Bool, iequal(a.s, "True") || std::strtol(a.s.c_str(), nullptr, 10) != 0);
        case 0x4C: { Value v = Value::of(VT::Float); v.f = std::strtof(a.s.c_str(), nullptr); return v; }
        case 0x4D: {  // StringToVector, "x,y,z"
            Value v = Value::of(VT::Vector);
            std::sscanf(a.s.c_str(), "%f,%f,%f", &v.v[0], &v.v[1], &v.v[2]);
            return v;
        }
        case 0x4E: {  // StringToRotator
            Value v = Value::of(VT::Rotator);
            std::sscanf(a.s.c_str(), "%d,%d,%d", &v.r[0], &v.r[1], &v.r[2]);
            return v;
        }
        case 0x4F: return make(VT::Bool, a.v[0] != 0 || a.v[1] != 0 || a.v[2] != 0);  // VectorToBool
        case 0x50: {  // VectorToRotator
            Value v = Value::of(VT::Rotator);
            v.r[0] = to_int(std::atan2(a.v[2], std::sqrt(a.v[0] * a.v[0] + a.v[1] * a.v[1])) / kRotToRad);
            v.r[1] = to_int(std::atan2(a.v[1], a.v[0]) / kRotToRad);
            return v;
        }
        case 0x51: return make(VT::Bool, a.r[0] || a.r[1] || a.r[2]);  // RotatorToBool
        case 0x52: case 0x53: return str(std::to_string(a.i));        // ByteToString, IntToString
        case 0x54: return str(a.i ? "True" : "False");                 // BoolToString
        case 0x55: return str(fmt("%f", double(a.f)));                 // FloatToString (unverified: 6 decimals)
        case 0x56:  // ObjectToString
            if (a.i == 0) return str("None");
            throw std::runtime_error("object to string not supported yet");
        case 0x57: return str(a.s);  // NameToString
        case 0x58:  // VectorToString
            return str(fmt("%f", double(a.v[0])) + "," + fmt("%f", double(a.v[1])) + "," + fmt("%f", double(a.v[2])));
        case 0x59:
            return str(std::to_string(a.r[0]) + "," + std::to_string(a.r[1]) + "," + std::to_string(a.r[2]));
        default: throw std::runtime_error("unknown conversion token " + std::to_string(t));
    }
}

// Reads one property declaration (an export of class *Property): name, type, flags, array size.
Local read_local(const Package& p, int32_t ref, int32_t* next) {
    const Export& e = p.exports[size_t(ref - 1)];
    size_t end = 0;
    read_properties(p, e, &end);
    Reader r(p.data, end);
    r.index();          // super
    *next = r.index();
    Local l;
    l.name = e.name;
    l.ref = ref;
    std::string cls = object_name(p, e.class_ref);
    if (cls.size() < 8 || cls.compare(cls.size() - 8, 8, "Property") != 0) return l;  // not a property
    l.dim = r.u16();
    r.u16();  // element size
    l.flags = r.u32();
    r.index();  // category
    if (l.flags & 0x20) r.u16();  // rep offset
    std::string struct_name;
    if (cls == "StructProperty") struct_name = object_name(p, r.index());
    l.type = type_of(cls, struct_name);
    l.type_name = struct_name.empty() ? cls : struct_name;
    if (l.dim < 1 || l.dim > 4096) throw std::runtime_error("bad array dimension");
    return l;
}

}  // namespace

Function load_function(const Package& p, int32_t ref) {
    if (ref <= 0 || size_t(ref) > p.exports.size()) throw std::runtime_error("function ref out of range");
    const Export& e = p.exports[size_t(ref - 1)];
    size_t end = 0;
    read_properties(p, e, &end);
    Reader r(p.data, end);
    Function f;
    f.pkg = &p;
    f.ref = ref;
    f.name = e.name;
    r.index(), r.index(), r.index();  // super, next, script text
    int32_t child = r.index();
    r.index();          // friendly name
    r.i32(), r.i32();   // line, text pos
    uint32_t size = r.u32();
    r.pos = skip_script(p, r.pos, size, &f.code);
    f.native = r.u16();
    r.u8();  // operator precedence
    f.flags = r.u32();
    for (size_t n = 0; child > 0; n++) {
        if (size_t(child) > p.exports.size() || n > p.exports.size()) throw std::runtime_error("bad function children");
        int32_t next = 0;
        f.locals.push_back(read_local(p, child, &next));
        child = next;
    }
    return f;
}

struct Vm::Frame {
    const Function& f;
    const Package* self_pkg;
    int32_t self_cls;
    std::vector<std::vector<Value>> vars;
    size_t pc = 0;
    bool done = false;

    void need(size_t n) const {
        if (pc + n > f.code.size()) throw std::runtime_error(f.name + ": bytecode runs past its end");
    }
    uint8_t peek() const { return need(1), f.code[pc]; }
    uint8_t u8() { return need(1), f.code[pc++]; }
    uint16_t u16() {
        need(2);
        uint16_t v = uint16_t(f.code[pc] | f.code[pc + 1] << 8);
        pc += 2;
        return v;
    }
    int32_t i32() {
        need(4);
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= uint32_t(f.code[pc + size_t(i)]) << (8 * i);
        pc += 4;
        return int32_t(v);
    }
    float f32() {
        uint32_t u = uint32_t(i32());
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    }
    std::string name() {
        int32_t i = i32();
        if (i < 0 || size_t(i) >= f.pkg->names.size()) throw std::runtime_error("bad name index in bytecode");
        return f.pkg->names[size_t(i)];
    }
    void jump(size_t to) {
        if (to > f.code.size()) throw std::runtime_error(f.name + ": jump out of the function");
        pc = to;
    }
    std::vector<Value>& var(int32_t ref) {
        for (size_t k = 0; k < f.locals.size(); k++)
            if (f.locals[k].ref == ref) return vars[k];
        throw std::runtime_error(f.name + ": variable is not a local of this function");
    }
};

const Function& Vm::function(const Package& p, int32_t ref) {
    const Package* pkg = &p;
    if (ref < 0) {
        if (!packages_) throw std::runtime_error("import without a package set");
        Object o = packages_->resolve(p, ref);
        if (!o.exp) throw std::runtime_error("unresolved function import " + object_name(p, ref));
        pkg = o.package;
        ref = int32_t(o.exp - pkg->exports.data()) + 1;
    }
    auto key = std::make_pair(pkg, ref);
    auto it = functions_.find(key);
    if (it == functions_.end()) it = functions_.emplace(key, load_function(*pkg, ref)).first;
    return it->second;
}

void Vm::bind_natives(const Package& p) {
    for (size_t i = 0; i < p.exports.size(); i++) {
        if (object_name(p, p.exports[i].class_ref) != "Function") continue;
        const Function& f = function(p, int32_t(i + 1));
        if (!f.native) continue;
        auto impl = by_name_.find(lower(f.name));
        by_index_[f.native] = {&f, impl == by_name_.end() ? nullptr : &impl->second};
    }
}

const Function* Vm::find_virtual(const Package& p0, int32_t cls, const std::string& name) {
    const Package* p = &p0;
    for (int guard = 0; guard < 64 && cls != 0; guard++) {
        if (cls < 0) {  // super class in another package
            if (!packages_) return nullptr;
            Object o = packages_->resolve(*p, cls);
            if (!o.exp) return nullptr;
            p = o.package;
            cls = int32_t(o.exp - p->exports.data()) + 1;
        }
        if (size_t(cls) > p->exports.size()) throw std::runtime_error("class ref out of range");
        for (size_t i = 0; i < p->exports.size(); i++) {
            const Export& e = p->exports[i];
            if (e.package == cls && iequal(e.name, name) && object_name(*p, e.class_ref) == "Function")
                return &function(*p, int32_t(i + 1));
        }
        cls = read_class(*p, p->exports[size_t(cls - 1)]).super_ref;
    }
    return nullptr;
}

Value* Vm::lvalue(Frame& fr) {
    uint8_t t = fr.u8();
    if (t == 0x00) return &fr.var(fr.i32())[0];
    if (t == 0x2D) return lvalue(fr);  // bool variable
    throw std::runtime_error(fr.f.name + ": token " + std::to_string(t) + " is not assignable yet");
}

Value Vm::call(const Function& f, std::vector<Value>& args) {
    int32_t cls = f.pkg->exports[size_t(f.ref - 1)].package;  // outer: the class, or a state inside it
    while (cls > 0 && size_t(cls) <= f.pkg->exports.size() && f.pkg->exports[size_t(cls - 1)].class_ref != 0)
        cls = f.pkg->exports[size_t(cls - 1)].package;
    Frame fr{f, f.pkg, cls, {}};
    if (f.flags & kFuncNative) throw std::runtime_error(f.name + " is native; call it from bytecode");
    struct Depth {  // released on exceptions too
        int& d;
        explicit Depth(int& v) : d(v) {
            if (++d > kMaxCallDepth) {
                --d;
                throw std::runtime_error("script recursion too deep");
            }
        }
        ~Depth() { --d; }
    } guard(depth_);
    size_t a = 0;
    for (const Local& l : f.locals) {
        fr.vars.emplace_back(size_t(l.dim), Value::of(l.type));
        if ((l.flags & kParm) && !(l.flags & kReturnParm) && a < args.size()) assign(fr.vars.back()[0], args[a++]);
    }
    while (!fr.done && fr.pc < f.code.size()) eval(fr);
    a = 0;
    Value ret;
    for (size_t k = 0; k < f.locals.size(); k++) {
        const Local& l = f.locals[k];
        if (l.flags & kReturnParm) ret = fr.vars[k][0];
        else if ((l.flags & kParm) && a < args.size()) args[a++] = fr.vars[k][0];  // out parameters come back
    }
    return ret;
}

Value Vm::call_script(Frame& fr, const Function& f) {
    std::vector<Value> args;
    std::vector<Value*> outs;
    for (const Local& l : f.locals) {
        if (!(l.flags & kParm) || (l.flags & kReturnParm) || fr.peek() == 0x16) continue;
        if (fr.peek() == 0x0B) {  // omitted optional parameter: zero
            fr.pc++;
            args.push_back(Value::of(l.type));
            outs.push_back(nullptr);
        } else if (l.flags & kOutParm) {
            outs.push_back(lvalue(fr));
            args.push_back(*outs.back());
        } else {
            args.push_back(eval(fr));
            outs.push_back(nullptr);
        }
    }
    if (fr.u8() != 0x16) throw std::runtime_error(fr.f.name + ": too many arguments to " + f.name);
    Value ret = call(f, args);
    for (size_t k = 0; k < outs.size(); k++)
        if (outs[k]) assign(*outs[k], args[k]);
    return ret;
}

Value Vm::call_native(Frame& fr, const Bound& b) {
    const Function& d = *b.decl;
    std::vector<Value> vals;
    std::vector<Value*> ptrs;
    vals.reserve(d.locals.size());  // pointers into vals must stay valid
    Value ret;
    bool ended = false;
    for (const Local& l : d.locals) {
        if (l.flags & kReturnParm) ret = Value::of(l.type);
        if (!(l.flags & kParm) || (l.flags & kReturnParm)) continue;
        if (fr.peek() == 0x16) {
            ptrs.push_back(nullptr);  // omitted trailing optional
        } else if (fr.peek() == 0x0B) {
            fr.pc++;
            ptrs.push_back(nullptr);
        } else if (l.flags & kOutParm) {
            ptrs.push_back(lvalue(fr));
        } else if ((l.flags & kSkipParm) && fr.peek() == 0x18) {
            // Right operand of && and ||, wrapped with a size that covers it and the closing 0x16 (short circuit).
            fr.pc++;
            uint16_t size = fr.u16();
            bool skip = !ptrs.empty() && ptrs[0] && (iequal(d.name, "AndAnd_BoolBool") ? !ptrs[0]->i : ptrs[0]->i);
            if (skip) fr.jump(fr.pc + size), ended = true;
            vals.push_back(skip ? Value::of(l.type) : eval(fr));
            ptrs.push_back(&vals.back());
        } else {
            vals.push_back(eval(fr));
            vals.back().t = l.type;
            ptrs.push_back(&vals.back());
        }
    }
    if (!ended && fr.u8() != 0x16) throw std::runtime_error(fr.f.name + ": too many arguments to native " + d.name);
    if (!b.impl) throw std::runtime_error("native " + d.name + " (" + std::to_string(d.native) + ") not implemented");
    (*b.impl)(ptrs, ret);
    return ret;
}

Value Vm::eval(Frame& fr) {
    uint8_t t = fr.peek();
    if (t == 0x00 || t == 0x2D) return *lvalue(fr);
    fr.pc++;
    switch (t) {
        case 0x04: {  // return
            Value v = eval(fr);
            for (size_t k = 0; k < fr.f.locals.size(); k++)
                if ((fr.f.locals[k].flags & kReturnParm) && v.t != VT::None) assign(fr.vars[k][0], v);
            fr.done = true;
            return Value();
        }
        case 0x05: {  // switch: evaluates the cases until one matches, then runs from there
            fr.u8();  // value size
            Value v = eval(fr);
            for (;;) {
                if (fr.pc >= fr.f.code.size() || fr.peek() != 0x0A) return Value();  // no case matched
                fr.pc++;
                uint16_t next = fr.u16();
                if (next == 0xFFFF || equal(v, eval(fr))) return Value();
                fr.jump(next);
            }
        }
        case 0x06: fr.jump(fr.u16()); return Value();
        case 0x07: {
            uint16_t to = fr.u16();
            if (!eval(fr).i) fr.jump(to);
            return Value();
        }
        case 0x08: fr.done = true; return Value();
        case 0x09: {
            uint16_t line = fr.u16();
            if (!eval(fr).i) throw std::runtime_error(fr.f.name + ": assertion failed at line " + std::to_string(line));
            return Value();
        }
        case 0x0A:  // reached by falling through: skip the case value
            if (fr.u16() != 0xFFFF) eval(fr);
            return Value();
        case 0x0B: return Value();
        case 0x0E: eval(fr); return Value();
        case 0x0F: case 0x14: {
            Value* d = lvalue(fr);
            assign(*d, eval(fr));
            return Value();
        }
        case 0x16: return Value();
        case 0x1B: {
            std::string n = fr.name();
            const Function* g = find_virtual(*fr.self_pkg, fr.self_cls, n);
            if (!g) throw std::runtime_error(fr.f.name + ": no function " + n);
            if (g->flags & kFuncNative) {
                auto it = by_name_.find(lower(g->name));
                return call_native(fr, {g, it == by_name_.end() ? nullptr : &it->second});
            }
            return call_script(fr, *g);
        }
        case 0x1C: {
            const Function& g = function(*fr.f.pkg, fr.i32());
            if (g.flags & kFuncNative) {
                auto it = by_name_.find(lower(g.name));
                return call_native(fr, {&g, it == by_name_.end() ? nullptr : &it->second});
            }
            return call_script(fr, g);
        }
        case 0x1D: return make(VT::Int, fr.i32());
        case 0x1E: { Value v = Value::of(VT::Float); v.f = fr.f32(); return v; }
        case 0x1F: {  // Latin-1, NUL-terminated
            std::u32string s;
            for (uint8_t c; (c = fr.u8()) != 0;) s.push_back(c);
            return str(encode(s));
        }
        case 0x20: return make(VT::Object, fr.i32());
        case 0x21: { Value v = Value::of(VT::Name); v.s = fr.name(); return v; }
        case 0x22: { Value v = Value::of(VT::Rotator); for (int32_t& c : v.r) c = fr.i32(); return v; }
        case 0x23: { Value v = Value::of(VT::Vector); for (float& c : v.v) c = fr.f32(); return v; }
        case 0x24: return make(VT::Byte, fr.u8());
        case 0x25: return make(VT::Int, 0);
        case 0x26: return make(VT::Int, 1);
        case 0x27: return make(VT::Bool, 1);
        case 0x28: return make(VT::Bool, 0);
        case 0x2A: return make(VT::Object, 0);
        case 0x2C: return make(VT::Int, fr.u8());
        case 0x34: {
            std::u32string s;
            for (uint16_t c; (c = fr.u16()) != 0;) s.push_back(c);
            return str(encode(s));
        }
        case 0x36: {  // struct member of Vector / Rotator
            std::string m = object_name(*fr.f.pkg, fr.i32());
            Value s = eval(fr);
            const char* vec[] = {"X", "Y", "Z"};
            const char* rot[] = {"Pitch", "Yaw", "Roll"};
            for (int k = 0; k < 3; k++) {
                if (s.t == VT::Vector && iequal(m, vec[k])) { Value v = Value::of(VT::Float); v.f = s.v[k]; return v; }
                if (s.t == VT::Rotator && iequal(m, rot[k])) return make(VT::Int, s.r[k]);
            }
            throw std::runtime_error(fr.f.name + ": struct member " + m + " not supported yet");
        }
        default:
            if (t >= 0x39 && t <= 0x59) return convert(t, eval(fr));
            if (t >= 0x60) {
                int index = t >= 0x70 ? t : (t - 0x60) * 256 + fr.u8();
                auto it = by_index_.find(index);
                if (it == by_index_.end()) throw std::runtime_error("native index " + std::to_string(index) + " not bound");
                return call_native(fr, it->second);
            }
            throw std::runtime_error(fr.f.name + ": bytecode token " + std::to_string(t) + " not supported yet");
    }
}

#define NATIVE(n) by_name_[lower(#n)] = []([[maybe_unused]] std::vector<Value*>& a, [[maybe_unused]] Value& r)

Vm::Vm(PackageSet* packages) : packages_(packages) {
    // Semantics from the declarations in Core (names, parameter types) and public UnrealScript documentation.
    // Bool
    NATIVE(Not_PreBool) { r.i = !a[0]->i; };
    NATIVE(AndAnd_BoolBool) { r.i = a[0]->i && a[1]->i; };
    NATIVE(OrOr_BoolBool) { r.i = a[0]->i || a[1]->i; };
    NATIVE(XorXor_BoolBool) { r.i = !a[0]->i != !a[1]->i; };
    NATIVE(EqualEqual_BoolBool) { r.i = !a[0]->i == !a[1]->i; };
    NATIVE(NotEqual_BoolBool) { r.i = !a[0]->i != !a[1]->i; };
    // Byte
    NATIVE(AddAdd_PreByte) { r.i = a[0]->i = (a[0]->i + 1) & 0xFF; };
    NATIVE(SubtractSubtract_PreByte) { r.i = a[0]->i = (a[0]->i - 1) & 0xFF; };
    NATIVE(AddAdd_Byte) { r.i = a[0]->i; a[0]->i = (a[0]->i + 1) & 0xFF; };
    NATIVE(SubtractSubtract_Byte) { r.i = a[0]->i; a[0]->i = (a[0]->i - 1) & 0xFF; };
    NATIVE(AddEqual_ByteByte) { r.i = a[0]->i = (a[0]->i + a[1]->i) & 0xFF; };
    NATIVE(SubtractEqual_ByteByte) { r.i = a[0]->i = (a[0]->i - a[1]->i) & 0xFF; };
    NATIVE(MultiplyEqual_ByteByte) { r.i = a[0]->i = (a[0]->i * a[1]->i) & 0xFF; };
    NATIVE(DivideEqual_ByteByte) { r.i = a[0]->i = a[1]->i ? (a[0]->i / a[1]->i) & 0xFF : 0; };
    // Int
    NATIVE(Complement_PreInt) { r.i = ~a[0]->i; };
    NATIVE(Subtract_PreInt) { r.i = wrap(-int64_t(a[0]->i)); };
    NATIVE(AddAdd_PreInt) { r.i = a[0]->i = wrap(int64_t(a[0]->i) + 1); };
    NATIVE(SubtractSubtract_PreInt) { r.i = a[0]->i = wrap(int64_t(a[0]->i) - 1); };
    NATIVE(AddAdd_Int) { r.i = a[0]->i; a[0]->i = wrap(int64_t(a[0]->i) + 1); };
    NATIVE(SubtractSubtract_Int) { r.i = a[0]->i; a[0]->i = wrap(int64_t(a[0]->i) - 1); };
    NATIVE(Multiply_IntInt) { r.i = wrap(int64_t(a[0]->i) * a[1]->i); };
    NATIVE(Divide_IntInt) { r.i = a[1]->i ? wrap(int64_t(a[0]->i) / a[1]->i) : 0; };  // shortcut: /0 gives 0, unverified
    NATIVE(Add_IntInt) { r.i = wrap(int64_t(a[0]->i) + a[1]->i); };
    NATIVE(Subtract_IntInt) { r.i = wrap(int64_t(a[0]->i) - a[1]->i); };
    NATIVE(LessLess_IntInt) { r.i = int32_t(uint32_t(a[0]->i) << (a[1]->i & 31)); };
    NATIVE(GreaterGreater_IntInt) { r.i = a[0]->i >> (a[1]->i & 31); };
    NATIVE(GreaterGreaterGreater_IntInt) { r.i = int32_t(uint32_t(a[0]->i) >> (a[1]->i & 31)); };
    NATIVE(Less_IntInt) { r.i = a[0]->i < a[1]->i; };
    NATIVE(Greater_IntInt) { r.i = a[0]->i > a[1]->i; };
    NATIVE(LessEqual_IntInt) { r.i = a[0]->i <= a[1]->i; };
    NATIVE(GreaterEqual_IntInt) { r.i = a[0]->i >= a[1]->i; };
    NATIVE(EqualEqual_IntInt) { r.i = a[0]->i == a[1]->i; };
    NATIVE(NotEqual_IntInt) { r.i = a[0]->i != a[1]->i; };
    NATIVE(And_IntInt) { r.i = a[0]->i & a[1]->i; };
    NATIVE(Xor_IntInt) { r.i = a[0]->i ^ a[1]->i; };
    NATIVE(Or_IntInt) { r.i = a[0]->i | a[1]->i; };
    NATIVE(AddEqual_IntInt) { r.i = a[0]->i = wrap(int64_t(a[0]->i) + a[1]->i); };
    NATIVE(SubtractEqual_IntInt) { r.i = a[0]->i = wrap(int64_t(a[0]->i) - a[1]->i); };
    NATIVE(MultiplyEqual_IntFloat) { r.i = a[0]->i = to_int(float(a[0]->i) * a[1]->f); };
    NATIVE(DivideEqual_IntFloat) { r.i = a[0]->i = a[1]->f != 0 ? to_int(float(a[0]->i) / a[1]->f) : 0; };
    NATIVE(Min) { r.i = std::min(a[0]->i, a[1]->i); };
    NATIVE(Max) { r.i = std::max(a[0]->i, a[1]->i); };
    NATIVE(Clamp) { r.i = std::max(a[1]->i, std::min(a[2]->i, a[0]->i)); };
    NATIVE(Rand) {
        static std::minstd_rand rng;  // shortcut: not the original generator; replays will differ
        r.i = a[0]->i > 0 ? int32_t(rng() % uint32_t(a[0]->i)) : 0;
    };
    // Float
    NATIVE(Subtract_PreFloat) { r.f = -a[0]->f; };
    NATIVE(MultiplyMultiply_FloatFloat) { r.f = std::pow(a[0]->f, a[1]->f); };
    NATIVE(Multiply_FloatFloat) { r.f = a[0]->f * a[1]->f; };
    NATIVE(Divide_FloatFloat) { r.f = a[1]->f != 0 ? a[0]->f / a[1]->f : 0; };  // shortcut: /0 gives 0, unverified
    NATIVE(Percent_FloatFloat) { r.f = a[1]->f != 0 ? std::fmod(a[0]->f, a[1]->f) : 0; };
    NATIVE(Add_FloatFloat) { r.f = a[0]->f + a[1]->f; };
    NATIVE(Subtract_FloatFloat) { r.f = a[0]->f - a[1]->f; };
    NATIVE(Less_FloatFloat) { r.i = a[0]->f < a[1]->f; };
    NATIVE(Greater_FloatFloat) { r.i = a[0]->f > a[1]->f; };
    NATIVE(LessEqual_FloatFloat) { r.i = a[0]->f <= a[1]->f; };
    NATIVE(GreaterEqual_FloatFloat) { r.i = a[0]->f >= a[1]->f; };
    NATIVE(EqualEqual_FloatFloat) { r.i = a[0]->f == a[1]->f; };
    NATIVE(NotEqual_FloatFloat) { r.i = a[0]->f != a[1]->f; };
    NATIVE(ComplementEqual_FloatFloat) { r.i = std::fabs(a[0]->f - a[1]->f) < 1e-4f; };  // tolerance unverified
    NATIVE(MultiplyEqual_FloatFloat) { r.f = a[0]->f *= a[1]->f; };
    NATIVE(DivideEqual_FloatFloat) { r.f = a[0]->f = a[1]->f != 0 ? a[0]->f / a[1]->f : 0; };
    NATIVE(AddEqual_FloatFloat) { r.f = a[0]->f += a[1]->f; };
    NATIVE(SubtractEqual_FloatFloat) { r.f = a[0]->f -= a[1]->f; };
    NATIVE(Abs) { r.f = std::fabs(a[0]->f); };
    NATIVE(Sin) { r.f = std::sin(a[0]->f); };
    NATIVE(Cos) { r.f = std::cos(a[0]->f); };
    NATIVE(Tan) { r.f = std::tan(a[0]->f); };
    NATIVE(Atan) { r.f = std::atan(a[0]->f); };
    NATIVE(Exp) { r.f = std::exp(a[0]->f); };
    NATIVE(Loge) { r.f = std::log(a[0]->f); };
    NATIVE(Sqrt) { r.f = std::sqrt(a[0]->f); };
    NATIVE(Square) { r.f = a[0]->f * a[0]->f; };
    NATIVE(FRand) {
        static std::minstd_rand rng;
        r.f = float(rng() - rng.min()) / float(rng.max() - rng.min());
    };
    NATIVE(FMin) { r.f = std::min(a[0]->f, a[1]->f); };
    NATIVE(FMax) { r.f = std::max(a[0]->f, a[1]->f); };
    NATIVE(FClamp) { r.f = std::max(a[1]->f, std::min(a[2]->f, a[0]->f)); };
    NATIVE(Lerp) { r.f = a[1]->f + a[0]->f * (a[2]->f - a[1]->f); };
    // Vector
    NATIVE(Subtract_PreVector) { for (int k = 0; k < 3; k++) r.v[k] = -a[0]->v[k]; };
    NATIVE(Multiply_VectorFloat) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] * a[1]->f; };
    NATIVE(Multiply_FloatVector) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->f * a[1]->v[k]; };
    NATIVE(Multiply_VectorVector) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] * a[1]->v[k]; };
    NATIVE(Divide_VectorFloat) { for (int k = 0; k < 3; k++) r.v[k] = a[1]->f != 0 ? a[0]->v[k] / a[1]->f : 0; };
    NATIVE(Add_VectorVector) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] + a[1]->v[k]; };
    NATIVE(Subtract_VectorVector) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] - a[1]->v[k]; };
    NATIVE(EqualEqual_VectorVector) { r.i = equal(*a[0], *a[1]); };
    NATIVE(NotEqual_VectorVector) { r.i = !equal(*a[0], *a[1]); };
    NATIVE(Dot_VectorVector) { r.f = a[0]->v[0] * a[1]->v[0] + a[0]->v[1] * a[1]->v[1] + a[0]->v[2] * a[1]->v[2]; };
    NATIVE(Cross_VectorVector) {
        const float *x = a[0]->v, *y = a[1]->v;
        r.v[0] = x[1] * y[2] - x[2] * y[1], r.v[1] = x[2] * y[0] - x[0] * y[2], r.v[2] = x[0] * y[1] - x[1] * y[0];
    };
    NATIVE(VSize) { r.f = std::sqrt(a[0]->v[0] * a[0]->v[0] + a[0]->v[1] * a[0]->v[1] + a[0]->v[2] * a[0]->v[2]); };
    NATIVE(Normal) {
        float n = std::sqrt(a[0]->v[0] * a[0]->v[0] + a[0]->v[1] * a[0]->v[1] + a[0]->v[2] * a[0]->v[2]);
        for (int k = 0; k < 3; k++) r.v[k] = n > 0 ? a[0]->v[k] / n : 0;
    };
    NATIVE(EqualEqual_RotatorRotator) { r.i = equal(*a[0], *a[1]); };
    NATIVE(NotEqual_RotatorRotator) { r.i = !equal(*a[0], *a[1]); };
    // String
    NATIVE(Concat_StrStr) { r.s = a[0]->s + a[1]->s; };
    NATIVE(At_StrStr) { r.s = a[0]->s + " " + a[1]->s; };
    NATIVE(Less_StrStr) { r.i = a[0]->s < a[1]->s; };
    NATIVE(Greater_StrStr) { r.i = a[0]->s > a[1]->s; };
    NATIVE(LessEqual_StrStr) { r.i = a[0]->s <= a[1]->s; };
    NATIVE(GreaterEqual_StrStr) { r.i = a[0]->s >= a[1]->s; };
    NATIVE(EqualEqual_StrStr) { r.i = a[0]->s == a[1]->s; };
    NATIVE(NotEqual_StrStr) { r.i = a[0]->s != a[1]->s; };
    NATIVE(ComplementEqual_StrStr) { r.i = iequal(a[0]->s, a[1]->s); };
    NATIVE(Len) { r.i = int32_t(decode(a[0]->s).size()); };
    NATIVE(InStr) {
        std::u32string s = decode(a[0]->s), t = decode(a[1]->s);
        size_t at = s.find(t);
        r.i = at == std::u32string::npos ? -1 : int32_t(at);
    };
    NATIVE(Mid) { r.s = substr(a[0]->s, a[1]->i, a[2] ? a[2]->i : INT32_MAX); };
    NATIVE(Left) { r.s = substr(a[0]->s, 0, a[1]->i); };
    NATIVE(Right) {
        int32_t n = int32_t(decode(a[0]->s).size());
        r.s = substr(a[0]->s, n - std::max(0, std::min(a[1]->i, n)), a[1]->i);
    };
    NATIVE(Caps) {  // shortcut: ASCII only, upgrade when a script needs accented capitals
        r.s = a[0]->s;
        for (char& c : r.s)
            if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    };
    NATIVE(Chr) { r.s = encode(std::u32string(1, char32_t(uint32_t(a[0]->i) & 0xFFFF))); };
    NATIVE(Asc) {
        std::u32string s = decode(a[0]->s);
        r.i = s.empty() ? 0 : int32_t(s[0]);
    };
    // Name, Object
    NATIVE(EqualEqual_NameName) { r.i = iequal(a[0]->s, a[1]->s); };
    NATIVE(NotEqual_NameName) { r.i = !iequal(a[0]->s, a[1]->s); };
    NATIVE(EqualEqual_ObjectObject) { r.i = a[0]->i == a[1]->i; };
    NATIVE(NotEqual_ObjectObject) { r.i = a[0]->i != a[1]->i; };
    // Log
    by_name_["log"] = [this](std::vector<Value*>& a, Value&) {
        log.push_back((a[1] ? a[1]->s : std::string("ScriptLog")) + ": " + a[0]->s);
    };
    by_name_["warn"] = [this](std::vector<Value*>& a, Value&) { log.push_back("ScriptWarning: " + a[0]->s); };
}

}  // namespace ut
