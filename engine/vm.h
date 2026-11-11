// UnrealScript VM: runs function bytecode from the user's packages (ADR 0006). Spec: docs/formats/script.md (execution)
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "packages.h"
#include "upkg.h"

namespace ut {

enum class VT : uint8_t { None, Byte, Int, Bool, Float, Name, Str, Object, Vector, Rotator, Struct };

struct Obj;

struct Value {
    VT t = VT::None;
    int32_t i = 0;    // Byte, Int, Bool
    float f = 0;      // Float
    std::string s;    // Name, Str
    Obj* o = nullptr;  // Object (nullptr = None)
    float v[3]{};     // Vector X Y Z
    int32_t r[3]{};   // Rotator Pitch Yaw Roll
    std::vector<Value> fields;                          // Struct members
    const std::vector<std::string>* field_names = nullptr;  // owned by the Vm, one list per struct
    static Value of(VT t) { Value x; x.t = t; return x; }
};

// A package object (class, property, texture...) or a script-created instance. Owned by the Vm.
struct Obj {
    const Package* pkg = nullptr;  // package object: its export; instance: nullptr
    int32_t ref = 0;
    Obj* cls = nullptr;            // instance: its class (a package object of a class export)
    Obj* defaults = nullptr;       // class object: its default object, built on first use
    std::map<const Obj*, std::vector<Value>> vars;  // property declaration -> elements
    // States: the current state, and where its code continues (code of `code_state`, which may be a super state).
    Obj* state = nullptr;
    Obj* code_state = nullptr;
    size_t state_pc = 0;
    bool code_active = false;
    float sleep = -1;    // latent Sleep: seconds left; < 0 = not sleeping
    std::function<bool()> wait;  // other latent natives: state code resumes once this returns true
    int state_gen = 0;   // bumped by every GotoState, so running code notices a state change
    std::vector<std::string> disabled;  // probe events turned off with Disable (lower case)
};

enum : uint32_t { kParm = 0x80, kOptionalParm = 0x10, kOutParm = 0x100, kSkipParm = 0x200, kReturnParm = 0x400 };
enum : uint32_t { kFuncNative = 0x400 };

struct Local {  // a property declaration: parameter, return value, local, instance variable or struct member
    std::string name;
    int32_t ref = 0;  // export ref of the declaration
    VT type = VT::None;
    std::string type_name;   // property class, or struct name for StructProperty
    int32_t struct_ref = 0;  // StructProperty: the struct (package-local ref)
    uint32_t flags = 0;
    int dim = 1;
    int32_t next = 0;  // next field of the same owner
};

struct Function {
    const Package* pkg = nullptr;
    int32_t ref = 0;
    std::string name;
    uint16_t native = 0;
    uint32_t flags = 0;
    std::vector<Local> locals;  // declaration order: parameters, return value, locals
    std::vector<uint8_t> code;  // in-memory form (see skip_script)
    uint16_t label_table = 0xFFFF;  // State: offset of the first label entry (after the 0x0C token); 0xFFFF = none
    uint32_t state_flags = 0;       // State: 0x2 = auto
};

Function load_function(const Package& p, int32_t ref);  // ref: export of class Function
Function load_state(const Package& p, int32_t ref);     // ref: export of class State; locals stay empty
enum : uint32_t { kFuncIterator = 0x4, kFuncLatent = 0x8, kStateAuto = 0x2 };

class Vm {
public:
    using Native = std::function<void(Obj* self, std::vector<Value*>& args, Value& ret)>;
    explicit Vm(PackageSet* packages);  // registers the built-in natives by declared name
    // Native index -> declaration, from the native functions of p (Core, Engine...).
    void bind_natives(const Package& p);
    // Runs f on self (an instance, or nullptr for code that touches no object). args: one per parameter, out
    // parameters written back. Returns the return value.
    Value call(const Function& f, Obj* self, std::vector<Value>& args);
    const Function& function(const Package& p, int32_t ref);  // loads and caches; follows imports
    Obj* object(const Package& p, int32_t ref);  // canonical object behind a reference (imports resolved); 0 -> nullptr
    Obj* spawn(Obj* cls);                        // new instance with the class defaults
    Obj* defaults(Obj* cls);                     // the class default object
    Obj* super_of(Obj* cls);                     // super class, or parent state
    // GotoState: EndState in the old state, BeginState in the new one, then its code runs from label. state "None"
    // leaves every state, "Auto" picks the class's auto state. Returns false (and logs) if the state is unknown.
    bool goto_state(Obj* o, const std::string& state, const std::string& label = "Begin");
    void tick(Obj* o, float dt);  // runs state code until it stops, sleeps or changes state
    // Calls the script function name on o (through its state) if it exists; returns whether it did.
    bool event(Obj* o, const std::string& name, std::vector<Value> args = {}, Value* ret = nullptr);
    Value* prop(Obj* o, const std::string& name);  // first element of a variable by name; nullptr if none
    std::vector<Value>* prop_array(Obj* o, const std::string& name);  // all elements of a static array
    void instantiate(Obj* o);  // package object (e.g. a map actor) -> live instance: class defaults + its properties
    void add_native(const std::string& name, Native fn);  // by declared name; replaces a built-in one
    bool is_a(Obj* cls, const Obj* base);  // cls is base or a subclass (or a state inside it)
    Obj* class_of(Obj* o);                 // an instance's class, or the class of a package object's export
    std::string name_of(const Obj* o) const;
    // Iterator natives (ForEach) put the values to visit here; the VM loops over them.
    std::vector<Value> iterator_items;
    std::vector<std::vector<Value>> iterator_extra;  // optional: per item, values for the further out parameters
    std::vector<std::string> log;  // Log() output and warnings, "Tag: text"
    std::vector<std::string> trace_calls;  // lower-case function names logged as "Call: Class.Function (state)"

private:
    struct Frame;
    struct Ref {  // assignable place: a value, or one component of a Vector / Rotator; v = nullptr: skipped (None)
        Value* v = nullptr;
        int comp = -1;
        std::vector<Value>* array = nullptr;  // the variable's elements, for indexing
    };
    struct Bound {
        const Function* decl = nullptr;
        const Native* impl = nullptr;
    };
    Value eval(Frame& fr, Obj* ctx);
    Ref lvalue(Frame& fr, Obj* ctx);
    std::vector<Value>& var(Obj* o, const Package& p, int32_t prop_ref);
    Value context(Frame& fr, Obj* ctx, bool cls, Ref* place);
    Value call_native(Frame& fr, const Bound& b, Obj* self);
    Value call_script(Frame& fr, Obj* target, const Function& f);
    Value call_named(Frame& fr, Obj* target, const Function& f);
    const Function* find_virtual(Obj* cls, Obj* state, const std::string& name);  // state first, unless nullptr
    Obj* find_state(Obj* cls, const std::string& name, bool autostate);
    const Function& state_code(Obj* st);
    Obj* find_prop(Obj* cls, const std::string& name);
    void apply_props(Obj* cls, const Package& p, const std::vector<Property>& tags, Obj* target);
    const Local& decl(Obj* prop);
    Value zero(const Package& p, const Local& l);
    void apply_defaults(Obj* cls, Obj* d);
    void warn(const Frame& fr, const std::string& what);

    PackageSet* packages_;
    std::map<std::string, Native> by_name_;  // lower-case declared name
    std::map<int, Bound> by_index_;
    std::map<std::pair<const Package*, int32_t>, Function> functions_;
    std::map<std::pair<const Package*, int32_t>, std::unique_ptr<Obj>> objects_;
    std::vector<std::unique_ptr<Obj>> instances_;
    std::map<const Obj*, Local> decls_;
    std::map<const Obj*, Obj*> supers_;
    std::map<std::pair<const Obj*, std::string>, Obj*> props_;
    std::map<std::string, std::unique_ptr<Obj>> intrinsic_;  // classes with no export (Core.Class, Engine.Sound...)
    std::vector<Ref> iterator_outs_;  // out parameters of the last iterator native called
    std::map<const Obj*, std::vector<std::string>> struct_names_;
    std::map<std::tuple<const Obj*, const Obj*, std::string>, const Function*> virtuals_;
    int depth_ = 0;
};

// UnrealScript names compare without case (ASCII).
bool iequal(const std::string& a, const std::string& b);

// Axes of a rotator (Pitch, Yaw, Roll; 65536 units per turn) as unit vectors: X forward, Y right, Z up. Local ->
// world is x * X + y * Y + z * Z (public UE1 rotation matrix, unverified).
void rotation_axes(const int32_t rot[3], float x[3], float y[3], float z[3]);

}  // namespace ut
