// Tyl Compiler - Compile-Time Function Evaluation (CTFE) Interpreter
// Evaluates comptime functions at compile time

#ifndef TYL_CTFE_INTERPRETER_H
#define TYL_CTFE_INTERPRETER_H

#include "frontend/ast/ast.h"
#include <unordered_map>
#include <variant>
#include <optional>
#include <stdexcept>

namespace tyl {

// Forward declarations for type metadata
struct TypeFieldInfo {
    std::string name;
    std::string typeName;
};

struct TypeMethodInfo {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params;
};

struct TypeMetadata {
    std::string name;
    std::vector<TypeFieldInfo> fields;
    std::vector<TypeMethodInfo> methods;
    size_t size = 0;
    size_t alignment = 0;
};

// CTFE Interpreter Value types (distinct from optimizer's CTFEValue)
struct CTFEInterpList;
struct CTFEInterpRecord;
struct CTFEInterpTuple;

using CTFEInterpValue = std::variant<
    int64_t,                              // Integer
    double,                               // Float
    bool,                                 // Boolean
    std::string,                          // String
    std::shared_ptr<CTFEInterpList>,      // List
    std::shared_ptr<CTFEInterpRecord>,    // Record
    std::shared_ptr<CTFEInterpTuple>,     // Tuple (for field info pairs)
    std::monostate                        // Nil/void
>;

struct CTFEInterpList {
    std::vector<CTFEInterpValue> elements;
};

struct CTFEInterpRecord {
    std::unordered_map<std::string, CTFEInterpValue> fields;
};

struct CTFEInterpTuple {
    std::vector<CTFEInterpValue> elements;
};

// Exception for CTFE errors
class CTFEInterpError : public std::runtime_error {
public:
    explicit CTFEInterpError(const std::string& msg) : std::runtime_error(msg) {}
};

// CTFE Interpreter - evaluates comptime functions at compile time
class CTFEInterpreter {
public:
    CTFEInterpreter() = default;
    
    // Register a comptime function for later evaluation
    void registerComptimeFunction(FnDecl* fn);
    
    // Check if a function is registered as comptime
    bool isComptimeFunction(const std::string& name) const;
    
    // Get a registered comptime function
    FnDecl* getComptimeFunction(const std::string& name) const;
    
    // Evaluate a comptime function call with given arguments
    // Returns nullopt if evaluation fails or function is not comptime
    std::optional<CTFEInterpValue> evaluateCall(const std::string& fnName, 
                                                const std::vector<CTFEInterpValue>& args);
    
    // Evaluate an expression at compile time
    // Returns nullopt if expression cannot be evaluated at compile time
    std::optional<CTFEInterpValue> evaluateExpr(Expression* expr);
    
    // Convert CTFEInterpValue to int64_t (for use in codegen)
    static std::optional<int64_t> toInt(const CTFEInterpValue& val);
    
    // Convert CTFEInterpValue to double
    static std::optional<double> toFloat(const CTFEInterpValue& val);
    
    // Convert CTFEInterpValue to string
    static std::optional<std::string> toString(const CTFEInterpValue& val);
    
    // Convert CTFEInterpValue to bool
    static std::optional<bool> toBool(const CTFEInterpValue& val);
    
    // Check if a value is truthy
    static bool isTruthy(const CTFEInterpValue& val);
    
    // Get cached evaluation result for a constant expression
    std::optional<CTFEInterpValue> getCachedResult(const std::string& key) const;
    
    // Cache an evaluation result
    void cacheResult(const std::string& key, const CTFEInterpValue& val);
    
    // Set maximum recursion depth (default 1000)
    void setMaxRecursionDepth(size_t depth) { maxRecursionDepth_ = depth; }
    
    // Set maximum iterations for loops (default 100000)
    void setMaxIterations(size_t iters) { maxIterations_ = iters; }
    
    // Compile-Time Reflection API
    // Register type metadata for reflection
    void registerTypeMetadata(const std::string& typeName, const TypeMetadata& metadata);
    
    // Get type metadata (returns nullptr if not found)
    const TypeMetadata* getTypeMetadata(const std::string& typeName) const;
    
    // Reflection builtins
    std::optional<CTFEInterpValue> evaluateFieldsOf(const std::string& typeName);
    std::optional<CTFEInterpValue> evaluateMethodsOf(const std::string& typeName);
    std::optional<CTFEInterpValue> evaluateTypeName(const std::string& typeName);
    std::optional<CTFEInterpValue> evaluateTypeSize(const std::string& typeName);
    std::optional<CTFEInterpValue> evaluateTypeAlign(const std::string& typeName);
    std::optional<CTFEInterpValue> evaluateHasField(const std::string& typeName, const std::string& fieldName);
    std::optional<CTFEInterpValue> evaluateHasMethod(const std::string& typeName, const std::string& methodName);
    std::optional<CTFEInterpValue> evaluateFieldType(const std::string& typeName, const std::string& fieldName);

private:
    // Registered comptime functions
    std::unordered_map<std::string, FnDecl*> comptimeFunctions_;
    
    // Cached evaluation results
    std::unordered_map<std::string, CTFEInterpValue> cache_;
    
    // Type metadata for compile-time reflection
    std::unordered_map<std::string, TypeMetadata> typeMetadata_;
    
    // Current variable scope during evaluation
    std::vector<std::unordered_map<std::string, CTFEInterpValue>> scopes_;
    
    // Recursion tracking
    size_t currentRecursionDepth_ = 0;
    size_t maxRecursionDepth_ = 1000;
    
    // Iteration tracking
    size_t totalIterations_ = 0;
    size_t maxIterations_ = 100000;
    
    // Loop control flags
    bool continueFlag_ = false;
    bool breakFlag_ = false;
    
    // Helper methods
    void pushScope();
    void popScope();
    void bindParameter(const std::string& name, const CTFEInterpValue& val);
    void setVariable(const std::string& name, const CTFEInterpValue& val);
    std::optional<CTFEInterpValue> getVariable(const std::string& name) const;
    
    // Statement evaluation (returns value for return statements)
    std::optional<CTFEInterpValue> evaluateStmt(Statement* stmt);
    std::optional<CTFEInterpValue> evaluateBlock(Block* block);
    
    // Evaluate statement with continue/break handling
    // Returns pair: (hasReturnValue, returnValue)
    std::pair<bool, std::optional<CTFEInterpValue>> evaluateStmtWithContinue(Statement* stmt);
    
    // Expression evaluation helpers
    CTFEInterpValue evaluateBinaryExpr(BinaryExpr* expr);
    CTFEInterpValue evaluateUnaryExpr(UnaryExpr* expr);
    CTFEInterpValue evaluateCallExpr(CallExpr* expr);
    CTFEInterpValue evaluateIndexExpr(IndexExpr* expr);
    CTFEInterpValue evaluateTernaryExpr(TernaryExpr* expr);
    CTFEInterpValue evaluateListExpr(ListExpr* expr);
    
    // Built-in function evaluation
    std::optional<CTFEInterpValue> evaluateBuiltin(const std::string& name, 
                                                   const std::vector<CTFEInterpValue>& args);
};

// Global CTFE interpreter instance
CTFEInterpreter& getGlobalCTFEInterpreter();

} // namespace tyl

#endif // TYL_CTFE_INTERPRETER_H
