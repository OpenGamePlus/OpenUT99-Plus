// UnrealScript VM: runs function bytecode from the user's packages (ADR 0006). Spec: docs/formats/script.md (execution)
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "packages.h"
#include "upkg.h"

namespace ut {

enum class VT : uint8_t { None, Byte, Int, Bool, Float, Name, Str, Object, Vector, Rotator };

struct Value {
    VT t = VT::None;
    int32_t i = 0;    // Byte, Int, Bool, Object (package-local ref, 0 = None)
    float f = 0;      // Float
    std::string s;    // Name, Str
    float v[3]{};     // Vector X Y Z
    int32_t r[3]{};   // Rotator Pitch Yaw Roll
    static Value of(VT t) { Value x; x.t = t; return x; }
};

enum : uint32_t { kParm = 0x80, kOptionalParm = 0x10, kOutParm = 0x100, kSkipParm = 0x200, kReturnParm = 0x400 };
enum : uint32_t { kFuncNative = 0x400 };

struct Local {  // parameter, return value or local variable of a function
    std::string name;
    int32_t ref = 0;  // export ref of the property declaration
    VT type = VT::None;
    std::string type_name;  // property class, or struct name for StructProperty
    uint32_t flags = 0;
    int dim = 1;
};

struct Function {
    const Package* pkg = nullptr;
    int32_t ref = 0;
    std::string name;
    uint16_t native = 0;
    uint32_t flags = 0;
    std::vector<Local> locals;  // declaration order: parameters, return value, locals
    std::vector<uint8_t> code;  // in-memory form (see skip_script)
};

Function load_function(const Package& p, int32_t ref);  // ref: export of class Function

class Vm {
public:
    using Native = std::function<void(std::vector<Value*>& args, Value& ret)>;
    explicit Vm(PackageSet* packages);  // registers the built-in natives by declared name
    // Native index -> declaration, from the native functions of p (Core, Engine...).
    void bind_natives(const Package& p);
    // Runs f with self of f's class. args: one per parameter, out parameters written back. Returns the return value.
    Value call(const Function& f, std::vector<Value>& args);
    const Function& function(const Package& p, int32_t ref);  // loads and caches; follows imports
    std::vector<std::string> log;  // Log() output, "Tag: text"

private:
    struct Frame;
    struct Bound {
        const Function* decl = nullptr;
        const Native* impl = nullptr;
    };
    Value eval(Frame& fr);
    Value* lvalue(Frame& fr);
    Value call_native(Frame& fr, const Bound& b);
    Value call_script(Frame& fr, const Function& f);
    const Function* find_virtual(const Package& p, int32_t cls_ref, const std::string& name);

    PackageSet* packages_;
    std::map<std::string, Native> by_name_;  // lower-case declared name
    std::map<int, Bound> by_index_;
    std::map<std::pair<const Package*, int32_t>, Function> functions_;
    int depth_ = 0;
};

}  // namespace ut
