// Tyl Compiler - Runtime Values
#ifndef TYL_VALUE_H
#define TYL_VALUE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <sstream>
#include <cstdint>
#include <cmath>
#include <type_traits>

#include "common/common.h"

namespace tyl {

enum class ValueType { NIL, BOOL, INT, FLOAT, STRING, LIST, RECORD, FUNCTION, NATIVE_FN, RANGE };

struct Value;
struct FlexFunction;
struct FlexRecord;

using NativeFn = std::function<Value(const std::vector<Value>&)>;

struct FlexFunction { std::string name; std::vector<std::string> params; size_t codeStart; size_t codeEnd; };
struct FlexRange { int64_t start; int64_t end; int64_t step; };

struct Value {
    ValueType type = ValueType::NIL;
    union { bool boolVal; int64_t intVal; double floatVal; };
    std::string stringVal;
    std::vector<Value> listVal;
    std::unordered_map<std::string, Value> recordVal;
    std::shared_ptr<FlexFunction> funcVal;
    NativeFn nativeVal;
    FlexRange rangeVal;
    
    Value() : type(ValueType::NIL), intVal(0) {}
    explicit Value(bool v) : type(ValueType::BOOL), boolVal(v) {}
    Value(int64_t v) : type(ValueType::INT), intVal(v) {}
    Value(int v) : type(ValueType::INT), intVal(v) {}  // Handle int literals
    Value(double v) : type(ValueType::FLOAT), floatVal(v) {}
    Value(const std::string& v) : type(ValueType::STRING), intVal(0), stringVal(v) {}
    Value(const char* v) : type(ValueType::STRING), intVal(0), stringVal(v) {}  // Handle string literals
    Value(std::vector<Value> v) : type(ValueType::LIST), intVal(0), listVal(std::move(v)) {}
    Value(std::shared_ptr<FlexFunction> f) : type(ValueType::FUNCTION), intVal(0), funcVal(std::move(f)) {}
    Value(NativeFn f) : type(ValueType::NATIVE_FN), intVal(0), nativeVal(std::move(f)) {}
    Value(FlexRange r) : type(ValueType::RANGE), rangeVal(r) {}
    
    // Template constructor for lambdas - converts to NativeFn
    template<typename F, typename = std::enable_if_t<
        std::is_invocable_r_v<Value, F, const std::vector<Value>&> &&
        !std::is_same_v<std::decay_t<F>, NativeFn> &&
        !std::is_same_v<std::decay_t<F>, Value>>>
    Value(F&& f) : type(ValueType::NATIVE_FN), intVal(0), nativeVal(std::forward<F>(f)) {}
    
    // Helper for creating bool values
    static Value makeBool(bool v) { return Value(v); }
    
    static Value makeRecord() { Value v; v.type = ValueType::RECORD; return v; }
    
    bool isTruthy() const {
        switch (type) {
            case ValueType::NIL: return false; case ValueType::BOOL: return boolVal;
            case ValueType::INT: return intVal != 0; case ValueType::FLOAT: return floatVal != 0.0;
            case ValueType::STRING: return !stringVal.empty(); case ValueType::LIST: return !listVal.empty();
            default: return true;
        }
    }
    
    std::string toString() const {
        switch (type) {
            case ValueType::NIL: return "nil"; case ValueType::BOOL: return boolVal ? "true" : "false";
            case ValueType::INT: return std::to_string(intVal);
            case ValueType::FLOAT: { std::ostringstream oss; oss << floatVal; return oss.str(); }
            case ValueType::STRING: return stringVal;
            case ValueType::LIST: { std::string s = "["; for (size_t i = 0; i < listVal.size(); i++) { if (i > 0) s += ", "; s += listVal[i].toString(); } return s + "]"; }
            case ValueType::RECORD: { std::string s = "{"; bool first = true; for (auto& [k, v] : recordVal) { if (!first) s += ", "; s += k + ": " + v.toString(); first = false; } return s + "}"; }
            case ValueType::FUNCTION: return "<fn " + funcVal->name + ">";
            case ValueType::NATIVE_FN: return "<native fn>";
            case ValueType::RANGE: return std::to_string(rangeVal.start) + ".." + std::to_string(rangeVal.end);
        }
        return "?";
    }
};

inline bool operator==(const Value& a, const Value& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case ValueType::NIL: return true; case ValueType::BOOL: return a.boolVal == b.boolVal;
        case ValueType::INT: return a.intVal == b.intVal; case ValueType::FLOAT: return a.floatVal == b.floatVal;
        case ValueType::STRING: return a.stringVal == b.stringVal; default: return false;
    }
}

} // namespace tyl

#endif // TYL_VALUE_H
