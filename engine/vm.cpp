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
    if (prop_class == "StructProperty") return VT::Struct;
    return VT::None;  // dynamic arrays, maps: not supported yet
}

bool equal(const Value& a, const Value& b) {
    switch (a.t) {
        case VT::Float: return a.f == b.f;
        case VT::Str: return a.s == b.s;
        case VT::Name: return iequal(a.s, b.s);
        case VT::Vector: return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2];
        case VT::Rotator: return a.r[0] == b.r[0] && a.r[1] == b.r[1] && a.r[2] == b.r[2];
        case VT::Object: return a.o == b.o;
        case VT::Bool: return !a.i == !b.i;
        case VT::Struct:
            if (a.fields.size() != b.fields.size()) return false;
            for (size_t k = 0; k < a.fields.size(); k++)
                if (!equal(a.fields[k], b.fields[k])) return false;
            return true;
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
        case 0x47: return make(VT::Bool, a.o != nullptr);      // ObjectToBool
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
        case 0x57: return str(a.s);  // NameToString
        case 0x58:  // VectorToString
            return str(fmt("%f", double(a.v[0])) + "," + fmt("%f", double(a.v[1])) + "," + fmt("%f", double(a.v[2])));
        case 0x59:
            return str(std::to_string(a.r[0]) + "," + std::to_string(a.r[1]) + "," + std::to_string(a.r[2]));
        default: throw std::runtime_error("unknown conversion token " + std::to_string(t));
    }
}

// Reads one property declaration (an export of class *Property): name, type, flags, array size.
Local read_local(const Package& p, int32_t ref) {
    const Export& e = p.exports[size_t(ref - 1)];
    size_t end = 0;
    read_properties(p, e, &end);
    Reader r(p.data, end);
    r.index();  // super
    Local l;
    l.next = r.index();
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
    if (cls == "StructProperty") {
        l.struct_ref = r.index();
        struct_name = object_name(p, l.struct_ref);
    }
    l.type = type_of(cls, struct_name);
    l.type_name = struct_name.empty() ? cls : struct_name;
    if (l.dim < 1) throw std::runtime_error("bad array dimension");  // u16: retail goes up to 16384
    return l;
}

// First member of a Struct export (also State, Class): tagged properties, super, next, script text, children.
int32_t struct_children(const Package& p, int32_t ref) {
    const Export& e = p.exports[size_t(ref - 1)];
    size_t end = size_t(e.offset);
    if (e.class_ref != 0) read_properties(p, e, &end);  // class objects have no leading property list
    Reader r(p.data, end);
    r.index(), r.index(), r.index();
    return r.index();
}

bool is_property(const Package& p, const Export& e) {
    std::string cls = object_name(p, e.class_ref);
    return cls.size() > 8 && cls.compare(cls.size() - 8, 8, "Property") == 0;
}

}  // namespace

namespace {

// Struct header and script of a Function or State; leaves r after the script.
Function load_struct(const Package& p, int32_t ref, Reader& r) {
    if (ref <= 0 || size_t(ref) > p.exports.size()) throw std::runtime_error("function ref out of range");
    const Export& e = p.exports[size_t(ref - 1)];
    size_t end = 0;
    read_properties(p, e, &end);
    r.pos = end;
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
    for (size_t n = 0; child > 0; n++) {
        if (size_t(child) > p.exports.size() || n > p.exports.size()) throw std::runtime_error("bad struct children");
        Local l = read_local(p, child);
        child = l.next;
        if (is_property(p, p.exports[size_t(l.ref - 1)])) f.locals.push_back(l);
    }
    return f;
}

}  // namespace

Function load_function(const Package& p, int32_t ref) {
    Reader r(p.data);
    Function f = load_struct(p, ref, r);
    f.native = r.u16();
    r.u8();  // operator precedence
    f.flags = r.u32();
    return f;
}

Function load_state(const Package& p, int32_t ref) {
    Reader r(p.data);
    Function f = load_struct(p, ref, r);
    f.locals.clear();  // a state's children are its functions
    r.u32(), r.u32(), r.u32(), r.u32();  // probe mask, ignore mask
    f.label_table = r.u16();
    f.state_flags = r.u32();
    if (f.label_table != 0xFFFF && f.label_table >= f.code.size()) throw std::runtime_error("label table outside the code");
    return f;
}

struct Vm::Frame {
    Frame(const Function& fn, Obj* s) : f(fn), self(s) {}
    const Function& f;
    Obj* self;
    std::vector<std::vector<Value>> vars;
    size_t pc = 0;
    bool done = false;
    struct Iter {  // an open ForEach
        std::vector<Value> items;
        std::vector<std::vector<Value>> extra;  // values for the other out parameters, per item (may be empty)
        size_t next = 0;
        std::vector<Ref> outs;
        size_t body = 0;
        void put(size_t k);  // writes item k into the out parameters
    };
    std::vector<Iter> iters;

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
    std::vector<Value>& local(int32_t ref) {
        for (size_t k = 0; k < f.locals.size(); k++)
            if (f.locals[k].ref == ref) return vars[k];
        throw std::runtime_error(f.name + ": variable is not a local of this function");
    }
};

namespace {

Value read(const Value* v, int comp) {
    if (!v) return Value();  // through a None context
    if (comp < 0) return *v;
    if (v->t == VT::Vector) {
        Value x = Value::of(VT::Float);
        x.f = v->v[comp];
        return x;
    }
    return make(VT::Int, v->r[comp]);
}

void write(Value* v, int comp, const Value& x) {
    if (!v) return;
    if (comp < 0) assign(*v, x);
    else if (v->t == VT::Vector) v->v[comp] = x.f;
    else v->r[comp] = x.i;
}

}  // namespace

void Vm::Frame::Iter::put(size_t k) {
    write(outs[0].v, outs[0].comp, items[k]);
    for (size_t j = 0; k < extra.size() && j < extra[k].size() && j + 1 < outs.size(); j++)
        write(outs[j + 1].v, outs[j + 1].comp, extra[k][j]);
}

namespace {

int member_index(const Value& s, const std::string& m) {  // component of Vector / Rotator, or struct field
    const char* vec[] = {"X", "Y", "Z"};
    const char* rot[] = {"Pitch", "Yaw", "Roll"};
    for (int k = 0; k < 3; k++)
        if ((s.t == VT::Vector && iequal(m, vec[k])) || (s.t == VT::Rotator && iequal(m, rot[k]))) return k;
    if (s.t == VT::Struct && s.field_names)
        for (size_t k = 0; k < s.field_names->size() && k < s.fields.size(); k++)
            if (iequal((*s.field_names)[k], m)) return int(k);
    throw std::runtime_error("no struct member " + m);
}

}  // namespace

const Function& Vm::function(const Package& p, int32_t ref) {
    Obj* o = object(p, ref);
    if (!o) throw std::runtime_error("function ref None");
    auto key = std::make_pair(o->pkg, o->ref);
    auto it = functions_.find(key);
    if (it == functions_.end()) it = functions_.emplace(key, load_function(*o->pkg, o->ref)).first;
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

Obj* Vm::object(const Package& p, int32_t ref) {
    if (ref == 0) return nullptr;
    const Package* pkg = &p;
    if (ref < 0) {
        if (!packages_) throw std::runtime_error("import without a package set: " + object_name(p, ref));
        Object o = packages_->resolve(p, ref);
        if (!o.exp) {
            const Import& im = p.imports[size_t(-ref - 1)];
            if (im.class_package == "Core" && im.class_name == "Class") {  // intrinsic class (C++ only, no export)
                std::unique_ptr<Obj>& slot = intrinsic_[lower(object_name(p, im.package) + "." + im.name)];
                if (!slot) slot = std::make_unique<Obj>();
                return slot.get();
            }
            throw std::runtime_error("unresolved import " + im.class_package + "." + im.class_name + " " +
                                     object_name(p, im.package) + "." + im.name);
        }
        pkg = o.package;
        ref = int32_t(o.exp - pkg->exports.data()) + 1;
    }
    if (size_t(ref) > pkg->exports.size()) throw std::runtime_error("object ref out of range");
    std::unique_ptr<Obj>& slot = objects_[{pkg, ref}];
    if (!slot) {
        slot = std::make_unique<Obj>();
        slot->pkg = pkg;
        slot->ref = ref;
    }
    return slot.get();
}

Obj* Vm::super_of(Obj* cls) {
    if (!cls || !cls->pkg) return nullptr;
    auto it = supers_.find(cls);
    if (it != supers_.end()) return it->second;
    const Export& e = cls->pkg->exports[size_t(cls->ref - 1)];
    size_t end = size_t(e.offset);
    if (e.class_ref != 0) read_properties(*cls->pkg, e, &end);  // states; class objects have no property list
    Obj* s = object(*cls->pkg, Reader(cls->pkg->data, end).index());
    supers_[cls] = s;
    return s;
}

const Local& Vm::decl(Obj* prop) {
    auto it = decls_.find(prop);
    if (it == decls_.end()) it = decls_.emplace(prop, read_local(*prop->pkg, prop->ref)).first;
    return it->second;
}

Value Vm::zero(const Package& p, const Local& l) {
    Value v = Value::of(l.type);
    if (l.type != VT::Struct || !l.struct_ref) return v;
    Obj* st = object(p, l.struct_ref);
    std::vector<std::string>& names = struct_names_[st];
    bool fresh = names.empty();
    std::vector<Obj*> chain;  // parent structs first (e.g. Plane extends Vector: X, Y, Z, then W)
    for (Obj* s = st; s && chain.size() < 64; s = super_of(s)) chain.insert(chain.begin(), s);
    // shortcut: static arrays inside structs keep their first element only
    for (Obj* s : chain) {
        size_t guard = 0;
        for (int32_t c = struct_children(*s->pkg, s->ref); c > 0; c = decl(object(*s->pkg, c)).next) {
            if (++guard > s->pkg->exports.size()) throw std::runtime_error("bad struct members");
            const Local& m = decl(object(*s->pkg, c));
            if (!is_property(*s->pkg, s->pkg->exports[size_t(c - 1)])) continue;
            v.fields.push_back(zero(*s->pkg, m));
            if (fresh) names.push_back(m.name);
        }
    }
    v.field_names = &names;
    return v;
}

std::vector<Value>& Vm::var(Obj* o, const Package& p, int32_t prop_ref) {
    Obj* prop = object(p, prop_ref);
    if (!prop) throw std::runtime_error("variable ref None");
    auto it = o->vars.find(prop);
    if (it == o->vars.end()) {
        const Local& l = decl(prop);
        it = o->vars.emplace(prop, std::vector<Value>(size_t(l.dim), zero(*prop->pkg, l))).first;
    }
    // Object's native variables are kept by the engine: Class, Name (shortcut: Outer not kept, writes dropped)
    const Export& e = prop->pkg->exports[size_t(prop->ref - 1)];
    if (e.package > 0 && prop->pkg->exports[size_t(e.package - 1)].name == "Object") {
        if (e.name == "Class") it->second[0].o = class_of(o);
        else if (e.name == "Name") it->second[0].s = name_of(o);
    }
    return it->second;
}

Obj* Vm::defaults(Obj* cls) {
    if (!cls) throw std::runtime_error("default object of None");
    if (!cls->defaults) {
        instances_.push_back(std::make_unique<Obj>());
        Obj* d = instances_.back().get();
        d->cls = cls;
        cls->defaults = d;
        if (Obj* s = super_of(cls)) d->vars = defaults(s)->vars;
        apply_defaults(cls, d);
    }
    return cls->defaults;
}

Obj* Vm::spawn(Obj* cls) {
    Obj* d = defaults(cls);
    instances_.push_back(std::make_unique<Obj>());
    Obj* o = instances_.back().get();
    o->cls = cls;
    o->vars = d->vars;
    return o;
}

Obj* Vm::find_prop(Obj* cls, const std::string& name) {
    auto key = std::make_pair(static_cast<const Obj*>(cls), lower(name));
    auto it = props_.find(key);
    if (it != props_.end()) return it->second;
    Obj* prop = nullptr;
    for (Obj* c = cls; c && !prop; c = super_of(c))
        for (size_t i = 0; i < c->pkg->exports.size() && !prop; i++) {
            const Export& e = c->pkg->exports[i];
            if (e.package == c->ref && iequal(e.name, name) && is_property(*c->pkg, e)) prop = object(*c->pkg, int32_t(i + 1));
        }
    props_[key] = prop;
    return prop;
}

Value* Vm::prop(Obj* o, const std::string& name) {
    Obj* p = o && o->cls ? find_prop(o->cls, name) : nullptr;
    return p ? &var(o, *p->pkg, p->ref)[0] : nullptr;
}

std::vector<Value>* Vm::prop_array(Obj* o, const std::string& name) {
    Obj* p = o && o->cls ? find_prop(o->cls, name) : nullptr;
    return p ? &var(o, *p->pkg, p->ref) : nullptr;
}

void Vm::instantiate(Obj* o) {
    if (o->cls || !o->pkg) return;
    const Export& e = o->pkg->exports[size_t(o->ref - 1)];
    o->cls = object(*o->pkg, e.class_ref);
    if (!o->cls) throw std::runtime_error("cannot instantiate a class object");
    o->vars = defaults(o->cls)->vars;
    size_t end = 0;
    apply_props(o->cls, *o->pkg, read_properties(*o->pkg, e, &end), o);
}

void Vm::apply_defaults(Obj* cls, Obj* d) {
    const Package& p = *cls->pkg;
    apply_props(cls, p, read_class(p, p.exports[size_t(cls->ref - 1)]).defaults, d);
}

void Vm::apply_props(Obj* cls, const Package& p, const std::vector<Property>& tags, Obj* d) {
    for (const Property& tag : tags) {
        Obj* prop = find_prop(cls, tag.name);
        if (!prop) continue;  // shortcut: a default for a property we cannot find is dropped, as for removed vars
        const Local& l = decl(prop);
        std::vector<Value>& slots = var(d, *prop->pkg, prop->ref);
        if (tag.index < 0 || size_t(tag.index) >= slots.size()) throw std::runtime_error("default index out of range");
        Value& v = slots[size_t(tag.index)];
        switch (tag.type) {
            case PropType::Byte: v.i = prop_byte(p, tag); break;
            case PropType::Int: v.i = prop_int(p, tag); break;
            case PropType::Bool: v.i = tag.bool_value; break;
            case PropType::Float: v.f = prop_float(p, tag); break;
            case PropType::Str: v.s = prop_string(p, tag); break;
            case PropType::Name: {
                int32_t n = prop_ref(p, tag);
                if (n < 0 || size_t(n) >= p.names.size()) throw std::runtime_error("bad name in defaults");
                v.s = p.names[size_t(n)];
                break;
            }
            case PropType::Object: case PropType::Class: v.o = object(p, prop_ref(p, tag)); break;
            case PropType::Struct:
                if (l.type == VT::Vector) {
                    Vec3 x = prop_vector(p, tag);
                    v.v[0] = x.x, v.v[1] = x.y, v.v[2] = x.z;
                } else if (l.type == VT::Rotator && tag.size >= 12) {
                    Reader r(p.data, tag.value_pos);
                    for (int32_t& c : v.r) c = r.i32();
                }  // shortcut: other struct defaults stay zero until their binary layout is decoded
                break;
            default: break;  // dynamic arrays, maps: not supported yet
        }
    }
}

const Function* Vm::find_virtual(Obj* cls, Obj* state, const std::string& name) {
    auto key = std::make_tuple(static_cast<const Obj*>(cls), static_cast<const Obj*>(state), lower(name));
    auto it = virtuals_.find(key);
    if (it != virtuals_.end()) return it->second;
    const Function* found = nullptr;
    int guard = 0;
    for (Obj* c = state; c && !found && guard++ < 256; c = super_of(c))  // the state, then its parent states
        for (size_t i = 0; i < c->pkg->exports.size() && !found; i++) {
            const Export& e = c->pkg->exports[i];
            if (e.package == c->ref && iequal(e.name, name) && object_name(*c->pkg, e.class_ref) == "Function")
                found = &function(*c->pkg, int32_t(i + 1));
        }
    for (Obj* c = cls; c && !found && guard++ < 512; c = super_of(c))
        for (size_t i = 0; i < c->pkg->exports.size() && !found; i++) {
            const Export& e = c->pkg->exports[i];
            if (e.package == c->ref && iequal(e.name, name) && object_name(*c->pkg, e.class_ref) == "Function")
                found = &function(*c->pkg, int32_t(i + 1));
        }
    virtuals_[key] = found;
    return found;
}

Obj* Vm::find_state(Obj* cls, const std::string& name, bool autostate) {
    for (Obj* c = cls; c; c = super_of(c))
        for (size_t i = 0; i < c->pkg->exports.size(); i++) {
            const Export& e = c->pkg->exports[i];
            if (e.package != c->ref || object_name(*c->pkg, e.class_ref) != "State") continue;
            Obj* st = object(*c->pkg, int32_t(i + 1));
            if (autostate ? (state_code(st).state_flags & kStateAuto) != 0 : iequal(e.name, name)) return st;
        }
    return nullptr;
}

const Function& Vm::state_code(Obj* st) {
    auto key = std::make_pair(st->pkg, st->ref);
    auto it = functions_.find(key);
    if (it == functions_.end()) it = functions_.emplace(key, load_state(*st->pkg, st->ref)).first;
    return it->second;
}

namespace {

// Offset of label in a state's label table; npos if absent. The header offset points at the first entry, just after
// the 0x0C token; entries are name index u32 + code offset u32 (in-memory form), up to a name "None".
size_t find_label(const Function& code, const std::string& label) {
    if (code.label_table == 0xFFFF) return std::string::npos;
    size_t at = code.label_table;
    if (at == 0 || code.code[at - 1] != 0x0C) throw std::runtime_error(code.name + ": no label table at its offset");
    for (;;) {
        if (at + 8 > code.code.size()) throw std::runtime_error(code.name + ": label table runs past the code");
        uint32_t n = 0, off = 0;
        for (int i = 0; i < 4; i++) n |= uint32_t(code.code[at + size_t(i)]) << (8 * i);
        for (int i = 0; i < 4; i++) off |= uint32_t(code.code[at + 4 + size_t(i)]) << (8 * i);
        at += 8;
        if (n >= code.pkg->names.size()) throw std::runtime_error("bad label name");
        const std::string& name = code.pkg->names[n];
        if (name == "None") return std::string::npos;
        if (iequal(name, label)) {
            if (off >= code.code.size()) throw std::runtime_error(code.name + ": label outside the code");
            return off;
        }
    }
}

}  // namespace

bool Vm::event(Obj* o, const std::string& name, std::vector<Value> args, Value* ret) {
    // shortcut: Disable applies to every event name, not only probe functions; state probe masks not applied
    if (std::find(o->disabled.begin(), o->disabled.end(), lower(name)) != o->disabled.end()) return false;
    const Function* f = find_virtual(o->cls, o->state, name);
    if (!f || (f->flags & kFuncNative)) return false;
    Value r = call(*f, o, args);
    if (ret) *ret = r;
    return true;
}

void Vm::add_native(const std::string& name, Native fn) { by_name_[lower(name)] = std::move(fn); }

bool Vm::goto_state(Obj* o, const std::string& name, const std::string& label) {
    bool leave = iequal(name, "None") || name.empty();
    Obj* st = leave ? nullptr : find_state(o->cls, name, iequal(name, "Auto"));
    if (!leave && !st) {
        if (!iequal(name, "Auto")) log.push_back("ScriptWarning: GotoState: no state " + name);  // no auto state is normal
        return false;
    }
    int gen = ++o->state_gen;
    o->code_active = false;
    o->sleep = -1;
    o->wait = nullptr;
    if (st != o->state) {
        if (o->state) event(o, "EndState");
        if (o->state_gen != gen) return true;  // EndState changed state itself
        o->state = st;
        if (st) event(o, "BeginState");
        if (o->state_gen != gen) return true;
    }
    for (Obj* s = st; s; s = super_of(s)) {  // code of the state, or of the nearest parent state that has labels
        const Function& code = state_code(s);
        if (code.label_table == 0xFFFF) continue;
        size_t at = find_label(code, label);
        if (at != std::string::npos) o->code_state = s, o->state_pc = at, o->code_active = true;
        break;
    }
    return true;
}

void Vm::tick(Obj* o, float dt) {
    if (!o->code_active) return;
    if (o->sleep >= 0) {
        o->sleep -= dt;
        if (Value* lf = prop(o, "LatentFloat")) lf->f = std::max(0.f, o->sleep);  // Actor: what scripts read
        if (o->sleep > 0) return;
        o->sleep = -1;
    }
    if (o->wait) {
        if (!o->wait()) return;
        o->wait = nullptr;
    }
    const Function& code = state_code(o->code_state);
    Frame fr(code, o);
    fr.pc = o->state_pc;
    int gen = o->state_gen;
    for (long steps = 0; fr.pc < code.code.size(); steps++) {
        if (steps > 1000000) throw std::runtime_error(code.name + ": runaway loop in state code");
        eval(fr, o);
        if (o->state_gen != gen) return;  // GotoState: the new code runs from the next tick
        if (fr.done) break;
        if (o->sleep >= 0 || o->wait) {  // latent call: continue after it
            o->state_pc = fr.pc;
            return;
        }
    }
    o->code_active = false;
}

void Vm::warn(const Frame& fr, const std::string& what) { log.push_back("ScriptWarning: " + fr.f.name + " " + what); }

Value Vm::context(Frame& fr, Obj* ctx, bool cls, Ref* place) {
    Obj* o = eval(fr, ctx).o;
    uint16_t skip = fr.u16();
    fr.u8();  // result size
    if (cls && o) o = defaults(o);
    if (!o) {  // None context: the expression is skipped and yields zero
        std::string what;  // the member it tried to reach, for the warning
        size_t at = fr.pc;
        if (at + 5 <= fr.f.code.size() && (fr.f.code[at] <= 0x02 || fr.f.code[at] == 0x1B || fr.f.code[at] == 0x1C)) {
            uint32_t ref = 0;
            for (int i = 0; i < 4; i++) ref |= uint32_t(fr.f.code[at + 1 + size_t(i)]) << (8 * i);
            what = fr.f.code[at] == 0x1B ? (ref < fr.f.pkg->names.size() ? fr.f.pkg->names[ref] : "?")
                                          : object_name(*fr.f.pkg, int32_t(ref));
        }
        warn(fr, "accessed None" + (what.empty() ? "" : " (" + what + ")"));
        fr.jump(fr.pc + skip);
        if (place) *place = Ref{};
        return Value();
    }
    if (place) {
        *place = lvalue(fr, o);
        return Value();
    }
    return eval(fr, o);
}

Vm::Ref Vm::lvalue(Frame& fr, Obj* ctx) {
    uint8_t t = fr.u8();
    auto slots = [&](uint8_t tok) -> std::vector<Value>& {  // 0x00 local, 0x01 instance, 0x02 default variable
        if (tok == 0x00) return fr.local(fr.i32());
        Obj* o = tok == 0x01 ? ctx : ctx && ctx->cls ? defaults(ctx->cls) : nullptr;
        if (!o) throw std::runtime_error(fr.f.name + ": object variable without an object");
        return var(o, *fr.f.pkg, fr.i32());
    };
    switch (t) {
        case 0x00: case 0x01: case 0x02: {
            std::vector<Value>& a = slots(t);
            return {&a[0], -1, &a};
        }
        case 0x2D: return lvalue(fr, ctx);  // bool variable
        case 0x1A: {  // array element: index with the caller's self, then the array in the context
            int32_t i = eval(fr, fr.self).i;
            Ref base = lvalue(fr, ctx);
            if (!base.v) return base;  // None context
            if (!base.array) throw std::runtime_error(fr.f.name + ": element of something that is not an array");
            std::vector<Value>& a = *base.array;
            if (i < 0 || size_t(i) >= a.size()) {  // shortcut: clamped with a warning, unverified against the original
                warn(fr, "accessed array out of bounds");
                i = i < 0 ? 0 : int32_t(a.size() - 1);
            }
            return {&a[size_t(i)]};
        }
        case 0x36: {
            std::string m = object_name(*fr.f.pkg, fr.i32());
            Ref base = lvalue(fr, ctx);
            if (!base.v) return base;
            if (base.comp >= 0) throw std::runtime_error(fr.f.name + ": member of a component");
            int k = member_index(*base.v, m);
            if (base.v->t == VT::Struct) return {&base.v->fields[size_t(k)]};
            return {base.v, k};
        }
        case 0x12: case 0x19: {
            Ref place;
            context(fr, ctx, t == 0x12, &place);
            return place;
        }
        default: throw std::runtime_error(fr.f.name + ": token " + std::to_string(t) + " is not assignable yet");
    }
}

Value Vm::call(const Function& f, Obj* self, std::vector<Value>& args) {
    if (f.flags & kFuncNative) throw std::runtime_error(f.name + " is native; call it from bytecode");
    if (!trace_calls.empty() && std::find(trace_calls.begin(), trace_calls.end(), lower(f.name)) != trace_calls.end())
        log.push_back("Call: " + name_of(self ? class_of(self) : nullptr) + "." + f.name + " (" + name_of(self ? self->state : nullptr) + ")");
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
    Frame fr(f, self);
    size_t a = 0;
    for (const Local& l : f.locals) {
        fr.vars.emplace_back(size_t(l.dim), zero(*f.pkg, l));
        if ((l.flags & kParm) && !(l.flags & kReturnParm) && a < args.size()) assign(fr.vars.back()[0], args[a++]);
    }
    while (!fr.done && fr.pc < f.code.size()) eval(fr, self);
    a = 0;
    Value ret;
    for (size_t k = 0; k < f.locals.size(); k++) {
        const Local& l = f.locals[k];
        if (l.flags & kReturnParm) ret = fr.vars[k][0];
        else if ((l.flags & kParm) && a < args.size()) args[a++] = fr.vars[k][0];  // out parameters come back
    }
    return ret;
}

Value Vm::call_script(Frame& fr, Obj* target, const Function& f) {
    std::vector<Value> args;
    std::vector<Ref> outs;
    for (const Local& l : f.locals) {
        if (!(l.flags & kParm) || (l.flags & kReturnParm) || fr.peek() == 0x16) continue;
        if (fr.peek() == 0x0B) {  // omitted optional parameter: zero
            fr.pc++;
            args.push_back(zero(*f.pkg, l));
            outs.push_back({});
        } else if (l.flags & kOutParm) {
            outs.push_back(lvalue(fr, fr.self));
            args.push_back(read(outs.back().v, outs.back().comp));
        } else {
            args.push_back(eval(fr, fr.self));
            outs.push_back({});
        }
    }
    if (fr.u8() != 0x16) throw std::runtime_error(fr.f.name + ": too many arguments to " + f.name);
    Value ret = call(f, target, args);
    for (size_t k = 0; k < outs.size(); k++) write(outs[k].v, outs[k].comp, args[k]);
    return ret;
}

Value Vm::call_named(Frame& fr, Obj* target, const Function& f) {
    if (!(f.flags & kFuncNative)) return call_script(fr, target, f);
    auto it = by_name_.find(lower(f.name));
    return call_native(fr, {&f, it == by_name_.end() ? nullptr : &it->second}, target);
}

Value Vm::call_native(Frame& fr, const Bound& b, Obj* self) {
    const Function& d = *b.decl;
    std::vector<Value> vals;
    std::vector<Value*> ptrs;
    std::vector<std::pair<Value*, Ref>> outs;  // copied back after the call
    vals.reserve(d.locals.size());  // pointers into vals must stay valid
    Value ret;
    bool ended = false;
    for (const Local& l : d.locals) {
        if (l.flags & kReturnParm) ret = Value::of(l.type);
        if (!(l.flags & kParm) || (l.flags & kReturnParm)) continue;
        if (ended || fr.peek() == 0x16) {
            ptrs.push_back(nullptr);  // omitted trailing optional
        } else if (fr.peek() == 0x0B) {
            fr.pc++;
            ptrs.push_back(nullptr);
        } else if (l.flags & kOutParm) {
            Ref place = lvalue(fr, fr.self);
            vals.push_back(read(place.v, place.comp));
            ptrs.push_back(&vals.back());
            outs.emplace_back(&vals.back(), place);
        } else if ((l.flags & kSkipParm) && fr.peek() == 0x18) {
            // Right operand of && and ||, wrapped with a size that covers it and the closing 0x16 (short circuit).
            fr.pc++;
            uint16_t size = fr.u16();
            bool skip = !ptrs.empty() && ptrs[0] && (iequal(d.name, "AndAnd_BoolBool") ? !ptrs[0]->i : ptrs[0]->i);
            if (skip) fr.jump(fr.pc + size), ended = true;
            vals.push_back(skip ? Value::of(l.type) : eval(fr, fr.self));
            ptrs.push_back(&vals.back());
        } else {
            vals.push_back(eval(fr, fr.self));
            vals.back().t = l.type;
            ptrs.push_back(&vals.back());
        }
    }
    if (!ended && fr.u8() != 0x16) throw std::runtime_error(fr.f.name + ": too many arguments to native " + d.name);
    if (!b.impl) throw std::runtime_error("native " + d.name + " (" + std::to_string(d.native) + ") not implemented");
    if (d.flags & kFuncIterator) {
        iterator_outs_.clear();
        for (auto& o : outs) iterator_outs_.push_back(o.second);
    }
    (*b.impl)(self, ptrs, ret);
    for (auto& [v, place] : outs) write(place.v, place.comp, *v);
    return ret;
}

std::string Vm::name_of(const Obj* o) const {
    if (!o) return "None";
    if (o->pkg) return o->pkg->exports[size_t(o->ref - 1)].name;
    if (!o->cls) {
        for (const auto& [n, slot] : intrinsic_)
            if (slot.get() == o) return n.substr(n.find('.') + 1);
        return "None";
    }
    return o->cls->pkg->exports[size_t(o->cls->ref - 1)].name;  // shortcut: instances are not numbered yet
}

Obj* Vm::class_of(Obj* o) {
    if (!o) return nullptr;
    if (o->cls || !o->pkg) return o->cls;
    int32_t c = o->pkg->exports[size_t(o->ref - 1)].class_ref;
    return c ? object(*o->pkg, c) : nullptr;  // shortcut: class objects (class 'Class') report no class
}

bool Vm::is_a(Obj* cls, const Obj* base) {
    for (int guard = 0; cls && guard < 256; cls = super_of(cls), guard++)
        if (cls == base) return true;
    return false;
}

Value Vm::eval(Frame& fr, Obj* ctx) {
    uint8_t t = fr.peek();
    if (t == 0x00 || t == 0x01 || t == 0x02 || t == 0x2D || t == 0x1A) {
        Ref place = lvalue(fr, ctx);
        return read(place.v, place.comp);
    }
    fr.pc++;
    switch (t) {
        case 0x04: {  // return
            Value v = eval(fr, fr.self);
            for (size_t k = 0; k < fr.f.locals.size(); k++)
                if ((fr.f.locals[k].flags & kReturnParm) && v.t != VT::None) assign(fr.vars[k][0], v);
            fr.done = true;
            return Value();
        }
        case 0x05: {  // switch: evaluates the cases until one matches, then runs from there
            fr.u8();  // value size
            Value v = eval(fr, fr.self);
            for (;;) {
                if (fr.pc >= fr.f.code.size() || fr.peek() != 0x0A) return Value();  // no case matched
                fr.pc++;
                uint16_t next = fr.u16();
                if (next == 0xFFFF || equal(v, eval(fr, fr.self))) return Value();
                fr.jump(next);
            }
        }
        case 0x06: fr.jump(fr.u16()); return Value();
        case 0x07: {
            uint16_t to = fr.u16();
            if (!eval(fr, fr.self).i) fr.jump(to);
            return Value();
        }
        case 0x08: fr.done = true; return Value();
        case 0x09: {
            uint16_t line = fr.u16();
            if (!eval(fr, fr.self).i) throw std::runtime_error(fr.f.name + ": assertion failed at line " + std::to_string(line));
            return Value();
        }
        case 0x0A:  // reached by falling through: skip the case value
            if (fr.u16() != 0xFFFF) eval(fr, fr.self);
            return Value();
        case 0x0B: return Value();
        case 0x0E: eval(fr, fr.self); return Value();
        case 0x0F: case 0x14: {
            Ref d = lvalue(fr, fr.self);
            write(d.v, d.comp, eval(fr, fr.self));
            return Value();
        }
        case 0x12: case 0x19: return context(fr, ctx, t == 0x12, nullptr);
        case 0x13: {  // class<Base>(expr): the class if it is Base or a subclass, else None
            Obj* base = object(*fr.f.pkg, fr.i32());
            Value c = eval(fr, fr.self);
            if (!is_a(c.o, base)) c.o = nullptr;
            return c;
        }
        case 0x2E: {  // Class(expr): the object if its class is Class or a subclass, else None
            Obj* base = object(*fr.f.pkg, fr.i32());
            Value x = eval(fr, fr.self);
            if (x.o && !is_a(class_of(x.o), base)) x.o = nullptr;
            x.t = VT::Object;
            return x;
        }
        case 0x2F: {  // ForEach: iterator native call, u16 offset of the loop's 0x30
            iterator_items.clear();
            iterator_extra.clear();
            iterator_outs_.clear();
            eval(fr, ctx);
            uint16_t end = fr.u16();
            if (iterator_outs_.empty()) iterator_outs_.push_back({});
            fr.iters.push_back({std::move(iterator_items), std::move(iterator_extra), 0, iterator_outs_, fr.pc});
            iterator_items.clear();
            iterator_extra.clear();
            Frame::Iter& it = fr.iters.back();
            if (it.items.empty()) fr.jump(end);
            else it.put(0);
            return Value();
        }
        case 0x31: {  // iterator next: back to the body while items remain
            if (fr.iters.empty()) throw std::runtime_error(fr.f.name + ": iterator next outside ForEach");
            Frame::Iter& it = fr.iters.back();
            if (++it.next < it.items.size()) {
                it.put(it.next);
                fr.jump(it.body);
            }
            return Value();
        }
        case 0x30:  // iterator pop: leaving the loop (end, break or return)
            if (fr.iters.empty()) throw std::runtime_error(fr.f.name + ": iterator pop outside ForEach");
            fr.iters.pop_back();
            return Value();
        case 0x16: return Value();
        case 0x17: { Value v = Value::of(VT::Object); v.o = fr.self; return v; }
        case 0x1B: case 0x38: {  // virtual call (the current state first), global call (class only)
            std::string n = fr.name();
            if (!ctx || !ctx->cls) throw std::runtime_error(fr.f.name + ": call to " + n + " without an object");
            const Function* g = find_virtual(ctx->cls, t == 0x1B ? ctx->state : nullptr, n);
            if (!g) throw std::runtime_error(fr.f.name + ": no function " + n);
            return call_named(fr, ctx, *g);
        }
        case 0x0C:  // label table: only reached if code runs past its end
            for (;;) {
                std::string n = fr.name();
                fr.i32();
                if (n == "None") return Value();
            }
        case 0x0D: {  // goto label, in state code
            std::string label = eval(fr, fr.self).s;
            size_t at = fr.f.label_table == 0xFFFF ? std::string::npos : find_label(fr.f, label);
            if (at == std::string::npos) throw std::runtime_error(fr.f.name + ": no label " + label);
            fr.jump(at);
            return Value();
        }
        case 0x1C: {
            const Function& g = function(*fr.f.pkg, fr.i32());
            return call_named(fr, ctx, g);
        }
        case 0x1D: return make(VT::Int, fr.i32());
        case 0x1E: { Value v = Value::of(VT::Float); v.f = fr.f32(); return v; }
        case 0x1F: {  // Latin-1, NUL-terminated
            std::u32string s;
            for (uint8_t c; (c = fr.u8()) != 0;) s.push_back(c);
            return str(encode(s));
        }
        case 0x20: { Value v = Value::of(VT::Object); v.o = object(*fr.f.pkg, fr.i32()); return v; }
        case 0x21: { Value v = Value::of(VT::Name); v.s = fr.name(); return v; }
        case 0x22: { Value v = Value::of(VT::Rotator); for (int32_t& c : v.r) c = fr.i32(); return v; }
        case 0x23: { Value v = Value::of(VT::Vector); for (float& c : v.v) c = fr.f32(); return v; }
        case 0x24: return make(VT::Byte, fr.u8());
        case 0x25: return make(VT::Int, 0);
        case 0x26: return make(VT::Int, 1);
        case 0x27: return make(VT::Bool, 1);
        case 0x28: return make(VT::Bool, 0);
        case 0x2A: return Value::of(VT::Object);
        case 0x2C: return make(VT::Int, fr.u8());
        case 0x34: {
            std::u32string s;
            for (uint16_t c; (c = fr.u16()) != 0;) s.push_back(c);
            return str(encode(s));
        }
        case 0x36: {  // struct member of a value that is not assignable (e.g. a function result)
            std::string m = object_name(*fr.f.pkg, fr.i32());
            Value s = eval(fr, ctx);
            if (s.t == VT::None) return Value();  // through a None context: zero
            int k = member_index(s, m);
            return s.t == VT::Struct ? s.fields[size_t(k)] : read(&s, k);
        }
        case 0x56: return str(name_of(eval(fr, fr.self).o));  // ObjectToString
        default:
            if (t >= 0x39 && t <= 0x59) return convert(t, eval(fr, fr.self));
            if (t >= 0x60) {
                int index = t >= 0x70 ? t : (t - 0x60) * 256 + fr.u8();
                auto it = by_index_.find(index);
                if (it == by_index_.end()) throw std::runtime_error("native index " + std::to_string(index) + " not bound");
                if (!it->second.impl) {  // natives may be added after bind_natives
                    auto impl = by_name_.find(lower(it->second.decl->name));
                    if (impl != by_name_.end()) it->second.impl = &impl->second;
                }
                return call_native(fr, it->second, ctx);
            }
            throw std::runtime_error(fr.f.name + ": bytecode token " + std::to_string(t) + " not supported yet");
    }
}

#define NATIVE(n) by_name_[lower(#n)] = []([[maybe_unused]] Obj* self, [[maybe_unused]] std::vector<Value*>& a, [[maybe_unused]] Value& r)

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
    NATIVE(AddEqual_VectorVector) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] += a[1]->v[k]; };
    NATIVE(SubtractEqual_VectorVector) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] -= a[1]->v[k]; };
    NATIVE(MultiplyEqual_VectorFloat) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] *= a[1]->f; };
    NATIVE(MultiplyEqual_VectorVector) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] *= a[1]->v[k]; };
    NATIVE(DivideEqual_VectorFloat) { for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[k] = a[1]->f != 0 ? a[0]->v[k] / a[1]->f : 0; };
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
    // Rotator: 65536 units per turn, components wrap like 32-bit ints.
    NATIVE(Add_RotatorRotator) { for (int k = 0; k < 3; k++) r.r[k] = wrap(int64_t(a[0]->r[k]) + a[1]->r[k]); };
    NATIVE(Subtract_RotatorRotator) { for (int k = 0; k < 3; k++) r.r[k] = wrap(int64_t(a[0]->r[k]) - a[1]->r[k]); };
    NATIVE(AddEqual_RotatorRotator) { for (int k = 0; k < 3; k++) r.r[k] = a[0]->r[k] = wrap(int64_t(a[0]->r[k]) + a[1]->r[k]); };
    NATIVE(SubtractEqual_RotatorRotator) { for (int k = 0; k < 3; k++) r.r[k] = a[0]->r[k] = wrap(int64_t(a[0]->r[k]) - a[1]->r[k]); };
    NATIVE(Multiply_RotatorFloat) { for (int k = 0; k < 3; k++) r.r[k] = to_int(float(a[0]->r[k]) * a[1]->f); };
    NATIVE(Multiply_FloatRotator) { for (int k = 0; k < 3; k++) r.r[k] = to_int(a[0]->f * float(a[1]->r[k])); };
    NATIVE(Divide_RotatorFloat) { for (int k = 0; k < 3; k++) r.r[k] = a[1]->f != 0 ? to_int(float(a[0]->r[k]) / a[1]->f) : 0; };
    NATIVE(MultiplyEqual_RotatorFloat) { for (int k = 0; k < 3; k++) r.r[k] = a[0]->r[k] = to_int(float(a[0]->r[k]) * a[1]->f); };
    NATIVE(DivideEqual_RotatorFloat) { for (int k = 0; k < 3; k++) r.r[k] = a[0]->r[k] = a[1]->f != 0 ? to_int(float(a[0]->r[k]) / a[1]->f) : 0; };
    auto axes = [](const Value& rot, float x[3], float y[3], float z[3]) { rotation_axes(rot.r, x, y, z); };
    by_name_["greatergreater_vectorrotator"] = [axes](Obj*, std::vector<Value*>& a, Value& r) {  // local -> world
        float x[3], y[3], z[3];
        axes(*a[1], x, y, z);
        for (int k = 0; k < 3; k++) r.v[k] = a[0]->v[0] * x[k] + a[0]->v[1] * y[k] + a[0]->v[2] * z[k];
    };
    by_name_["lessless_vectorrotator"] = [axes](Obj*, std::vector<Value*>& a, Value& r) {  // world -> local
        float x[3], y[3], z[3];
        axes(*a[1], x, y, z);
        const float* v = a[0]->v;
        r.v[0] = v[0] * x[0] + v[1] * x[1] + v[2] * x[2];
        r.v[1] = v[0] * y[0] + v[1] * y[1] + v[2] * y[2];
        r.v[2] = v[0] * z[0] + v[1] * z[1] + v[2] * z[2];
    };
    by_name_["getaxes"] = [axes](Obj*, std::vector<Value*>& a, Value&) { axes(*a[0], a[1]->v, a[2]->v, a[3]->v); };
    by_name_["getunaxes"] = [axes](Obj*, std::vector<Value*>& a, Value&) {
        float x[3], y[3], z[3];
        axes(*a[0], x, y, z);
        for (int k = 0; k < 3; k++) a[1]->v[k] = k == 0 ? x[0] : k == 1 ? y[0] : z[0];  // transposed axes
        for (int k = 0; k < 3; k++) a[2]->v[k] = k == 0 ? x[1] : k == 1 ? y[1] : z[1];
        for (int k = 0; k < 3; k++) a[3]->v[k] = k == 0 ? x[2] : k == 1 ? y[2] : z[2];
    };
    // DynamicLoadObject(string ObjectName "Package.[Group.]Name", class ObjectClass, optional bool MayFail)
    by_name_["dynamicloadobject"] = [this](Obj*, std::vector<Value*>& a, Value& r) {
        const std::string& path = a[0]->s;
        size_t dot = path.find('.'), last = path.rfind('.');
        const Package* p = dot == std::string::npos || !packages_ ? nullptr : packages_->get(path.substr(0, dot));
        for (size_t i = 0; p && i < p->exports.size() && !r.o; i++) {
            const Export& e = p->exports[i];
            if (!e.size || !iequal(e.name, path.substr(last + 1))) continue;
            std::string full = e.name;  // shortcut: matches the full group path, not partial paths
            for (int32_t o = e.package, g = 0; o > 0 && g < 64; o = p->exports[size_t(o - 1)].package, g++)
                full = p->exports[size_t(o - 1)].name + "." + full;
            if (!iequal(path.substr(0, dot) + "." + full, path)) continue;
            Obj* found = object(*p, int32_t(i + 1));
            Obj* cls = e.class_ref ? object(*p, e.class_ref) : nullptr;  // class objects: class 'Class'
            if (!a[1]->o || (e.class_ref ? is_a(cls, a[1]->o) : iequal(name_of(a[1]->o), "Class"))) r.o = found;
        }
        if (!r.o && !(a[2] && a[2]->i)) log.push_back("ScriptWarning: DynamicLoadObject failed: " + path);
    };
    by_name_["enable"] = [](Obj* self, std::vector<Value*>& a, Value&) {
        if (!self) return;
        auto& d = self->disabled;
        d.erase(std::remove(d.begin(), d.end(), lower(a[0]->s)), d.end());
    };
    by_name_["disable"] = [](Obj* self, std::vector<Value*>& a, Value&) {
        if (self && std::find(self->disabled.begin(), self->disabled.end(), lower(a[0]->s)) == self->disabled.end())
            self->disabled.push_back(lower(a[0]->s));
    };
    NATIVE(EqualEqual_RotatorRotator) { r.i = equal(*a[0], *a[1]); };
    NATIVE(NotEqual_RotatorRotator) { r.i = !equal(*a[0], *a[1]); };
    // String
    NATIVE(Concat_StrStr) { r.s = a[0]->s + a[1]->s; };
    NATIVE(At_StrStr) { r.s = a[0]->s + " " + a[1]->s; };
    NATIVE(ConcatEqual_StrStr) { r.s = a[0]->s += a[1]->s; };
    NATIVE(AtEqual_StrStr) { r.s = a[0]->s += " " + a[1]->s; };
    NATIVE(SubtractEqual_StrStr) {  // removes every occurrence of B from A
        if (!a[1]->s.empty())
            for (size_t at; (at = a[0]->s.find(a[1]->s)) != std::string::npos;) a[0]->s.erase(at, a[1]->s.size());
        r.s = a[0]->s;
    };
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
    NATIVE(EqualEqual_ObjectObject) { r.i = a[0]->o == a[1]->o; };
    NATIVE(NotEqual_ObjectObject) { r.i = a[0]->o != a[1]->o; };
    by_name_["isa"] = [this](Obj* self, std::vector<Value*>& a, Value& r) {  // IsA(name ClassName)
        for (Obj* c = class_of(self); c && !r.i; c = super_of(c)) r.i = iequal(name_of(c), a[0]->s);
    };
    by_name_["classischildof"] = [this](Obj*, std::vector<Value*>& a, Value& r) { r.i = a[0]->o && is_a(a[0]->o, a[1]->o); };
    by_name_["gotostate"] = [this](Obj* self, std::vector<Value*>& a, Value&) {
        if (self) goto_state(self, a[0] ? a[0]->s : "None", a[1] ? a[1]->s : "Begin");
    };
    by_name_["isinstate"] = [this](Obj* self, std::vector<Value*>& a, Value& r) {
        for (Obj* s = self ? self->state : nullptr; s && !r.i; s = super_of(s)) r.i = iequal(name_of(s), a[0]->s);
    };
    by_name_["getstatename"] = [this](Obj* self, std::vector<Value*>&, Value& r) {
        r.s = self && self->state ? name_of(self->state) : "None";
    };
    by_name_["sleep"] = [this](Obj* self, std::vector<Value*>& a, Value&) {  // latent: resumes once the time has passed
        if (!self) return;
        self->sleep = std::max(0.f, a[0]->f);  // Sleep(0): next tick
        if (Value* lf = prop(self, "LatentFloat")) lf->f = self->sleep;
    };
    // Log
    by_name_["log"] = [this](Obj*, std::vector<Value*>& a, Value&) {
        log.push_back((a[1] ? a[1]->s : std::string("ScriptLog")) + ": " + a[0]->s);
    };
    by_name_["warn"] = [this](Obj*, std::vector<Value*>& a, Value&) { log.push_back("ScriptWarning: " + a[0]->s); };
}

bool iequal(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

void rotation_axes(const int32_t rot[3], float x[3], float y[3], float z[3]) {
    float sp = std::sin(float(rot[0]) * kRotToRad), cp = std::cos(float(rot[0]) * kRotToRad);
    float sy = std::sin(float(rot[1]) * kRotToRad), cy = std::cos(float(rot[1]) * kRotToRad);
    float sr = std::sin(float(rot[2]) * kRotToRad), cr = std::cos(float(rot[2]) * kRotToRad);
    x[0] = cp * cy, x[1] = cp * sy, x[2] = sp;
    y[0] = sr * sp * cy - cr * sy, y[1] = sr * sp * sy + cr * cy, y[2] = -sr * cp;
    z[0] = -(cr * sp * cy + sr * sy), z[1] = cy * sr - cr * sp * sy, z[2] = cr * cp;
}

}  // namespace ut
