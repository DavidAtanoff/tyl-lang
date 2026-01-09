// Tyl Compiler - CTFE Interpreter Implementation

#include "ctfe_interpreter.h"
#include "frontend/token/token.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iostream>

// Temporary debug flag
static bool CTFE_DEBUG = false;

namespace tyl {

// Global CTFE interpreter instance
static CTFEInterpreter globalCTFE;

CTFEInterpreter& getGlobalCTFEInterpreter() {
    return globalCTFE;
}

void CTFEInterpreter::registerComptimeFunction(FnDecl* fn) {
    if (fn && fn->isComptime) {
        comptimeFunctions_[fn->name] = fn;
    }
}

bool CTFEInterpreter::isComptimeFunction(const std::string& name) const {
    return comptimeFunctions_.find(name) != comptimeFunctions_.end();
}

FnDecl* CTFEInterpreter::getComptimeFunction(const std::string& name) const {
    auto it = comptimeFunctions_.find(name);
    return it != comptimeFunctions_.end() ? it->second : nullptr;
}

void CTFEInterpreter::pushScope() {
    scopes_.emplace_back();
}

void CTFEInterpreter::popScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

// Bind a parameter in the current scope (always creates new, shadows outer)
void CTFEInterpreter::bindParameter(const std::string& name, const CTFEInterpValue& val) {
    if (scopes_.empty()) {
        scopes_.emplace_back();
    }
    // Always create in current scope, shadowing any outer variables
    scopes_.back()[name] = val;
}

void CTFEInterpreter::setVariable(const std::string& name, const CTFEInterpValue& val) {
    if (scopes_.empty()) {
        scopes_.emplace_back();
    }
    
    // Search from innermost to outermost scope for existing variable
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            // Update existing variable in its original scope
            found->second = val;
            return;
        }
    }
    
    // Variable doesn't exist anywhere - create in current scope
    scopes_.back()[name] = val;
}

std::optional<CTFEInterpValue> CTFEInterpreter::getVariable(const std::string& name) const {
    // Search from innermost to outermost scope
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

std::optional<int64_t> CTFEInterpreter::toInt(const CTFEInterpValue& val) {
    if (auto* i = std::get_if<int64_t>(&val)) return *i;
    if (auto* d = std::get_if<double>(&val)) return static_cast<int64_t>(*d);
    if (auto* b = std::get_if<bool>(&val)) return *b ? 1 : 0;
    return std::nullopt;
}

std::optional<double> CTFEInterpreter::toFloat(const CTFEInterpValue& val) {
    if (auto* d = std::get_if<double>(&val)) return *d;
    if (auto* i = std::get_if<int64_t>(&val)) return static_cast<double>(*i);
    return std::nullopt;
}

std::optional<std::string> CTFEInterpreter::toString(const CTFEInterpValue& val) {
    if (auto* s = std::get_if<std::string>(&val)) return *s;
    if (auto* i = std::get_if<int64_t>(&val)) return std::to_string(*i);
    if (auto* d = std::get_if<double>(&val)) return std::to_string(*d);
    if (auto* b = std::get_if<bool>(&val)) return *b ? "true" : "false";
    if (std::holds_alternative<std::monostate>(val)) return "nil";
    return std::nullopt;
}

std::optional<bool> CTFEInterpreter::toBool(const CTFEInterpValue& val) {
    if (auto* b = std::get_if<bool>(&val)) return *b;
    if (auto* i = std::get_if<int64_t>(&val)) return *i != 0;
    if (auto* d = std::get_if<double>(&val)) return *d != 0.0;
    if (auto* s = std::get_if<std::string>(&val)) return !s->empty();
    if (std::holds_alternative<std::monostate>(val)) return false;
    return true;  // Lists and records are truthy
}

bool CTFEInterpreter::isTruthy(const CTFEInterpValue& val) {
    auto b = toBool(val);
    return b.value_or(true);
}

std::optional<CTFEInterpValue> CTFEInterpreter::getCachedResult(const std::string& key) const {
    auto it = cache_.find(key);
    return it != cache_.end() ? std::optional<CTFEInterpValue>(it->second) : std::nullopt;
}

void CTFEInterpreter::cacheResult(const std::string& key, const CTFEInterpValue& val) {
    cache_[key] = val;
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateCall(const std::string& fnName,
                                                        const std::vector<CTFEInterpValue>& args) {
    // Enable debug for count_even_helper
    if (fnName == "count_even_helper" || fnName == "is_even") {
        CTFE_DEBUG = true;
    }
    
    if (CTFE_DEBUG) {
        std::cerr << "CTFE: evaluateCall(" << fnName << ") with " << args.size() << " args\n";
        for (size_t i = 0; i < args.size(); i++) {
            if (auto* iv = std::get_if<int64_t>(&args[i])) {
                std::cerr << "  arg[" << i << "] = " << *iv << "\n";
            }
        }
    }
    
    // Check for built-in functions first
    auto builtin = evaluateBuiltin(fnName, args);
    if (builtin) return builtin;
    
    // Look up comptime function
    auto* fn = getComptimeFunction(fnName);
    if (!fn || !fn->body) {
        if (CTFE_DEBUG) {
            std::cerr << "CTFE: Function " << fnName << " not found!\n";
        }
        return std::nullopt;
    }
    
    // Check recursion depth
    if (currentRecursionDepth_ >= maxRecursionDepth_) {
        throw CTFEInterpError("CTFE recursion depth exceeded");
    }
    
    currentRecursionDepth_++;
    
    // Reset iteration counter for this call
    totalIterations_ = 0;
    
    // Create new scope and bind parameters
    pushScope();
    
    for (size_t i = 0; i < fn->params.size() && i < args.size(); ++i) {
        bindParameter(fn->params[i].first, args[i]);
    }
    
    // Evaluate function body
    std::optional<CTFEInterpValue> result;
    try {
        result = evaluateStmt(fn->body.get());
    } catch (...) {
        popScope();
        currentRecursionDepth_--;
        CTFE_DEBUG = false;
        throw;
    }
    
    popScope();
    currentRecursionDepth_--;
    
    if (CTFE_DEBUG && (fnName == "count_even_helper" || fnName == "is_even")) {
        if (result && std::holds_alternative<int64_t>(*result)) {
            std::cerr << "CTFE: " << fnName << " returned " << std::get<int64_t>(*result) << "\n";
        }
        if (fnName == "is_even") {
            CTFE_DEBUG = false;
        }
    }
    
    return result.value_or(CTFEInterpValue{std::monostate{}});
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateExpr(Expression* expr) {
    if (!expr) return std::nullopt;
    
    // Integer literal
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return CTFEInterpValue{intLit->value};
    }
    
    // Float literal
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return CTFEInterpValue{floatLit->value};
    }

    // String literal
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return CTFEInterpValue{strLit->value};
    }
    
    // Bool literal
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return CTFEInterpValue{boolLit->value};
    }
    
    // Nil literal
    if (dynamic_cast<NilLiteral*>(expr)) {
        return CTFEInterpValue{std::monostate{}};
    }
    
    // Identifier - look up variable
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return getVariable(id->name);
    }
    
    // Assignment expression (x = value)
    if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(assignExpr->target.get())) {
            auto val = evaluateExpr(assignExpr->value.get());
            if (val) {
                setVariable(id->name, *val);
                return val;  // Assignment expressions return the assigned value
            }
        }
        return std::nullopt;
    }
    
    // Binary expression
    if (auto* binExpr = dynamic_cast<BinaryExpr*>(expr)) {
        try {
            return evaluateBinaryExpr(binExpr);
        } catch (const CTFEInterpError&) {
            return std::nullopt;
        }
    }
    
    // Unary expression
    if (auto* unaryExpr = dynamic_cast<UnaryExpr*>(expr)) {
        try {
            return evaluateUnaryExpr(unaryExpr);
        } catch (const CTFEInterpError&) {
            return std::nullopt;
        }
    }
    
    // Call expression
    if (auto* callExpr = dynamic_cast<CallExpr*>(expr)) {
        try {
            return evaluateCallExpr(callExpr);
        } catch (const CTFEInterpError&) {
            return std::nullopt;
        }
    }
    
    // Index expression
    if (auto* indexExpr = dynamic_cast<IndexExpr*>(expr)) {
        try {
            return evaluateIndexExpr(indexExpr);
        } catch (const CTFEInterpError&) {
            return std::nullopt;
        }
    }

    // Ternary expression
    if (auto* ternExpr = dynamic_cast<TernaryExpr*>(expr)) {
        try {
            return evaluateTernaryExpr(ternExpr);
        } catch (const CTFEInterpError&) {
            return std::nullopt;
        }
    }
    
    // List expression
    if (auto* listExpr = dynamic_cast<ListExpr*>(expr)) {
        try {
            return evaluateListExpr(listExpr);
        } catch (const CTFEInterpError&) {
            return std::nullopt;
        }
    }
    
    // TypeMetadataExpr - compile-time type introspection
    if (auto* metaExpr = dynamic_cast<TypeMetadataExpr*>(expr)) {
        const std::string& typeName = metaExpr->typeName;
        const std::string& kind = metaExpr->metadataKind;
        
        if (kind == "name") {
            return evaluateTypeName(typeName);
        } else if (kind == "size") {
            return evaluateTypeSize(typeName);
        } else if (kind == "align") {
            return evaluateTypeAlign(typeName);
        } else if (kind == "is_pod") {
            std::vector<CTFEInterpValue> args = {CTFEInterpValue{typeName}};
            return evaluateBuiltin("is_pod", args);
        } else if (kind == "is_primitive") {
            std::vector<CTFEInterpValue> args = {CTFEInterpValue{typeName}};
            return evaluateBuiltin("is_primitive", args);
        }
        return std::nullopt;
    }
    
    // Cannot evaluate at compile time
    return std::nullopt;
}

CTFEInterpValue CTFEInterpreter::evaluateBinaryExpr(BinaryExpr* expr) {
    auto leftOpt = evaluateExpr(expr->left.get());
    auto rightOpt = evaluateExpr(expr->right.get());
    
    if (!leftOpt || !rightOpt) {
        throw CTFEInterpError("Cannot evaluate binary expression operands at compile time");
    }
    
    CTFEInterpValue left = *leftOpt;
    CTFEInterpValue right = *rightOpt;
    
    // Handle string concatenation
    if (expr->op == TokenType::PLUS) {
        auto leftStr = std::get_if<std::string>(&left);
        auto rightStr = std::get_if<std::string>(&right);
        if (leftStr && rightStr) {
            return CTFEInterpValue{*leftStr + *rightStr};
        }
    }
    
    // Numeric operations
    auto leftInt = toInt(left);
    auto rightInt = toInt(right);
    auto leftFloat = toFloat(left);
    auto rightFloat = toFloat(right);

    // Integer operations
    if (leftInt && rightInt) {
        int64_t l = *leftInt;
        int64_t r = *rightInt;
        
        switch (expr->op) {
            case TokenType::PLUS: return CTFEInterpValue{l + r};
            case TokenType::MINUS: return CTFEInterpValue{l - r};
            case TokenType::STAR: return CTFEInterpValue{l * r};
            case TokenType::SLASH: 
                if (r == 0) throw CTFEInterpError("Division by zero in CTFE");
                return CTFEInterpValue{l / r};
            case TokenType::PERCENT:
                if (r == 0) throw CTFEInterpError("Modulo by zero in CTFE");
                return CTFEInterpValue{l % r};
            case TokenType::LT: return CTFEInterpValue{l < r};
            case TokenType::LE: return CTFEInterpValue{l <= r};
            case TokenType::GT: return CTFEInterpValue{l > r};
            case TokenType::GE: return CTFEInterpValue{l >= r};
            case TokenType::EQ: return CTFEInterpValue{l == r};
            case TokenType::NE: return CTFEInterpValue{l != r};
            case TokenType::AMP: return CTFEInterpValue{l & r};
            case TokenType::PIPE: return CTFEInterpValue{l | r};
            case TokenType::CARET: return CTFEInterpValue{l ^ r};
            default: break;
        }
    }
    
    // Float operations
    if (leftFloat && rightFloat) {
        double l = *leftFloat;
        double r = *rightFloat;
        
        switch (expr->op) {
            case TokenType::PLUS: return CTFEInterpValue{l + r};
            case TokenType::MINUS: return CTFEInterpValue{l - r};
            case TokenType::STAR: return CTFEInterpValue{l * r};
            case TokenType::SLASH:
                if (r == 0.0) throw CTFEInterpError("Division by zero in CTFE");
                return CTFEInterpValue{l / r};
            case TokenType::LT: return CTFEInterpValue{l < r};
            case TokenType::LE: return CTFEInterpValue{l <= r};
            case TokenType::GT: return CTFEInterpValue{l > r};
            case TokenType::GE: return CTFEInterpValue{l >= r};
            case TokenType::EQ: return CTFEInterpValue{l == r};
            case TokenType::NE: return CTFEInterpValue{l != r};
            default: break;
        }
    }

    // Boolean operations
    auto leftBool = toBool(left);
    auto rightBool = toBool(right);
    
    if (leftBool && rightBool) {
        switch (expr->op) {
            case TokenType::AND: return CTFEInterpValue{*leftBool && *rightBool};
            case TokenType::OR: return CTFEInterpValue{*leftBool || *rightBool};
            default: break;
        }
    }
    
    throw CTFEInterpError("Unsupported binary operation in CTFE");
}

CTFEInterpValue CTFEInterpreter::evaluateUnaryExpr(UnaryExpr* expr) {
    auto operandOpt = evaluateExpr(expr->operand.get());
    if (!operandOpt) {
        throw CTFEInterpError("Cannot evaluate unary expression operand at compile time");
    }
    
    CTFEInterpValue operand = *operandOpt;
    
    switch (expr->op) {
        case TokenType::MINUS: {
            if (auto i = toInt(operand)) return CTFEInterpValue{-*i};
            if (auto f = toFloat(operand)) return CTFEInterpValue{-*f};
            break;
        }
        case TokenType::BANG:
        case TokenType::NOT: {
            if (auto b = toBool(operand)) return CTFEInterpValue{!*b};
            break;
        }
        case TokenType::TILDE: {
            if (auto i = toInt(operand)) return CTFEInterpValue{~*i};
            break;
        }
        default: break;
    }
    
    throw CTFEInterpError("Unsupported unary operation in CTFE");
}

CTFEInterpValue CTFEInterpreter::evaluateCallExpr(CallExpr* expr) {
    // Get function name
    auto* id = dynamic_cast<Identifier*>(expr->callee.get());
    if (!id) {
        throw CTFEInterpError("Cannot evaluate non-identifier function call in CTFE");
    }

    // Evaluate arguments
    std::vector<CTFEInterpValue> args;
    for (auto& arg : expr->args) {
        auto argVal = evaluateExpr(arg.get());
        if (!argVal) {
            throw CTFEInterpError("Cannot evaluate function argument at compile time");
        }
        args.push_back(*argVal);
    }
    
    // Try to evaluate the call
    auto result = evaluateCall(id->name, args);
    if (!result) {
        throw CTFEInterpError("Cannot evaluate function '" + id->name + "' at compile time");
    }
    
    return *result;
}

CTFEInterpValue CTFEInterpreter::evaluateIndexExpr(IndexExpr* expr) {
    auto objOpt = evaluateExpr(expr->object.get());
    auto idxOpt = evaluateExpr(expr->index.get());
    
    if (!objOpt || !idxOpt) {
        throw CTFEInterpError("Cannot evaluate index expression at compile time");
    }
    
    // List indexing
    if (auto* list = std::get_if<std::shared_ptr<CTFEInterpList>>(&*objOpt)) {
        auto idx = toInt(*idxOpt);
        if (!idx) throw CTFEInterpError("List index must be an integer");
        
        // 1-based indexing (as per Tyl language)
        int64_t i = *idx - 1;
        if (i < 0 || static_cast<size_t>(i) >= (*list)->elements.size()) {
            throw CTFEInterpError("List index out of bounds");
        }
        return (*list)->elements[static_cast<size_t>(i)];
    }
    
    // String indexing
    if (auto* str = std::get_if<std::string>(&*objOpt)) {
        auto idx = toInt(*idxOpt);
        if (!idx) throw CTFEInterpError("String index must be an integer");
        
        // 1-based indexing
        int64_t i = *idx - 1;
        if (i < 0 || static_cast<size_t>(i) >= str->size()) {
            throw CTFEInterpError("String index out of bounds");
        }
        return CTFEInterpValue{std::string(1, (*str)[static_cast<size_t>(i)])};
    }
    
    throw CTFEInterpError("Cannot index non-list/string value in CTFE");
}

CTFEInterpValue CTFEInterpreter::evaluateTernaryExpr(TernaryExpr* expr) {
    auto condOpt = evaluateExpr(expr->condition.get());
    if (!condOpt) {
        throw CTFEInterpError("Cannot evaluate ternary condition at compile time");
    }
    
    if (isTruthy(*condOpt)) {
        auto thenOpt = evaluateExpr(expr->thenExpr.get());
        if (!thenOpt) throw CTFEInterpError("Cannot evaluate ternary then-branch");
        return *thenOpt;
    } else {
        auto elseOpt = evaluateExpr(expr->elseExpr.get());
        if (!elseOpt) throw CTFEInterpError("Cannot evaluate ternary else-branch");
        return *elseOpt;
    }
}

CTFEInterpValue CTFEInterpreter::evaluateListExpr(ListExpr* expr) {
    auto list = std::make_shared<CTFEInterpList>();
    for (auto& elem : expr->elements) {
        auto val = evaluateExpr(elem.get());
        if (!val) {
            throw CTFEInterpError("Cannot evaluate list element at compile time");
        }
        list->elements.push_back(*val);
    }
    return CTFEInterpValue{list};
}

std::pair<bool, std::optional<CTFEInterpValue>> CTFEInterpreter::evaluateStmtWithContinue(Statement* stmt) {
    continueFlag_ = false;
    breakFlag_ = false;
    
    auto result = evaluateStmt(stmt);
    
    if (continueFlag_) {
        continueFlag_ = false;
        return {false, std::nullopt};  // Continue - no return value
    }
    if (breakFlag_) {
        breakFlag_ = false;
        return {true, std::nullopt};  // Break - exit loop with no value
    }
    if (result) {
        return {true, result};  // Return statement - exit with value
    }
    return {false, std::nullopt};  // Normal execution - continue loop
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateStmt(Statement* stmt) {
    if (!stmt) return std::nullopt;
    
    // Block
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        return evaluateBlock(block);
    }
    
    // Return statement
    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        if (ret->value) {
            return evaluateExpr(ret->value.get());
        }
        return CTFEInterpValue{std::monostate{}};
    }
    
    // Variable declaration
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            auto val = evaluateExpr(varDecl->initializer.get());
            if (val) {
                setVariable(varDecl->name, *val);
            }
        }
        return std::nullopt;  // Continue execution
    }

    // Assignment statement
    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* id = dynamic_cast<Identifier*>(assign->target.get())) {
            auto val = evaluateExpr(assign->value.get());
            if (val) {
                setVariable(id->name, *val);
            }
        }
        return std::nullopt;
    }
    
    // Expression statement
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        evaluateExpr(exprStmt->expr.get());
        return std::nullopt;
    }
    
    // If statement
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto cond = evaluateExpr(ifStmt->condition.get());
        if (!cond) return std::nullopt;
        
        if (isTruthy(*cond)) {
            auto result = evaluateStmt(ifStmt->thenBranch.get());
            if (result) return result;
        } else {
            // Check elif branches
            for (auto& elif : ifStmt->elifBranches) {
                auto elifCond = evaluateExpr(elif.first.get());
                if (elifCond && isTruthy(*elifCond)) {
                    auto result = evaluateStmt(elif.second.get());
                    if (result) return result;
                    return std::nullopt;
                }
            }
            // Else branch
            if (ifStmt->elseBranch) {
                auto result = evaluateStmt(ifStmt->elseBranch.get());
                if (result) return result;
            }
        }
        return std::nullopt;
    }
    
    // While statement
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        while (true) {
            if (totalIterations_++ > maxIterations_) {
                throw CTFEInterpError("CTFE iteration limit exceeded");
            }
            
            auto cond = evaluateExpr(whileStmt->condition.get());
            if (!cond || !isTruthy(*cond)) break;
            
            auto result = evaluateStmtWithContinue(whileStmt->body.get());
            if (result.first) return result.second;  // Got a return value
            // If result.first is false, it was a continue - loop again
        }
        return std::nullopt;
    }
    
    // Continue statement - signal to continue the loop
    if (dynamic_cast<ContinueStmt*>(stmt)) {
        continueFlag_ = true;
        return std::nullopt;
    }
    
    // Break statement
    if (dynamic_cast<BreakStmt*>(stmt)) {
        breakFlag_ = true;
        return std::nullopt;
    }

    // For statement
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        auto iterableOpt = evaluateExpr(forStmt->iterable.get());
        if (!iterableOpt) return std::nullopt;
        
        // Handle range iteration
        if (auto* rangeExpr = dynamic_cast<RangeExpr*>(forStmt->iterable.get())) {
            auto startOpt = evaluateExpr(rangeExpr->start.get());
            auto endOpt = evaluateExpr(rangeExpr->end.get());
            
            if (!startOpt || !endOpt) return std::nullopt;
            
            auto start = toInt(*startOpt);
            auto end = toInt(*endOpt);
            
            if (!start || !end) return std::nullopt;
            
            int64_t step = 1;
            if (rangeExpr->step) {
                auto stepOpt = evaluateExpr(rangeExpr->step.get());
                if (stepOpt) {
                    auto s = toInt(*stepOpt);
                    if (s) step = *s;
                }
            }
            
            pushScope();
            for (int64_t i = *start; i < *end; i += step) {
                if (totalIterations_++ > maxIterations_) {
                    popScope();
                    throw CTFEInterpError("CTFE iteration limit exceeded");
                }
                
                setVariable(forStmt->var, CTFEInterpValue{i});
                auto result = evaluateStmt(forStmt->body.get());
                if (result) {
                    popScope();
                    return result;
                }
            }
            popScope();
            return std::nullopt;
        }
        
        // Handle list iteration
        if (auto* list = std::get_if<std::shared_ptr<CTFEInterpList>>(&*iterableOpt)) {
            pushScope();
            for (const auto& elem : (*list)->elements) {
                if (totalIterations_++ > maxIterations_) {
                    popScope();
                    throw CTFEInterpError("CTFE iteration limit exceeded");
                }
                
                setVariable(forStmt->var, elem);
                auto result = evaluateStmt(forStmt->body.get());
                if (result) {
                    popScope();
                    return result;
                }
            }
            popScope();
        }
        
        return std::nullopt;
    }
    
    return std::nullopt;
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateBlock(Block* block) {
    pushScope();
    
    for (auto& stmt : block->statements) {
        auto result = evaluateStmt(stmt.get());
        if (result) {
            popScope();
            return result;
        }
        // Propagate continue/break flags up
        if (continueFlag_ || breakFlag_) {
            popScope();
            return std::nullopt;
        }
    }
    
    popScope();
    return std::nullopt;
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateBuiltin(const std::string& name,
                                                          const std::vector<CTFEInterpValue>& args) {
    // len() - get length of string or list
    if (name == "len" && args.size() == 1) {
        if (auto* str = std::get_if<std::string>(&args[0])) {
            return CTFEInterpValue{static_cast<int64_t>(str->size())};
        }
        if (auto* list = std::get_if<std::shared_ptr<CTFEInterpList>>(&args[0])) {
            return CTFEInterpValue{static_cast<int64_t>((*list)->elements.size())};
        }
    }
    
    // abs() - absolute value
    if (name == "abs" && args.size() == 1) {
        if (auto i = toInt(args[0])) {
            return CTFEInterpValue{*i < 0 ? -*i : *i};
        }
        if (auto f = toFloat(args[0])) {
            return CTFEInterpValue{std::abs(*f)};
        }
    }
    
    // min() - minimum of two values
    if (name == "min" && args.size() == 2) {
        auto a = toInt(args[0]);
        auto b = toInt(args[1]);
        if (a && b) {
            return CTFEInterpValue{*a < *b ? *a : *b};
        }
        auto fa = toFloat(args[0]);
        auto fb = toFloat(args[1]);
        if (fa && fb) {
            return CTFEInterpValue{*fa < *fb ? *fa : *fb};
        }
    }
    
    // max() - maximum of two values
    if (name == "max" && args.size() == 2) {
        auto a = toInt(args[0]);
        auto b = toInt(args[1]);
        if (a && b) {
            return CTFEInterpValue{*a > *b ? *a : *b};
        }
        auto fa = toFloat(args[0]);
        auto fb = toFloat(args[1]);
        if (fa && fb) {
            return CTFEInterpValue{*fa > *fb ? *fa : *fb};
        }
    }

    // pow() - power function
    if (name == "pow" && args.size() == 2) {
        auto base = toInt(args[0]);
        auto exp = toInt(args[1]);
        if (base && exp && *exp >= 0 && *exp <= 63) {
            int64_t result = 1;
            for (int64_t i = 0; i < *exp; ++i) {
                result *= *base;
            }
            return CTFEInterpValue{result};
        }
        auto fbase = toFloat(args[0]);
        auto fexp = toFloat(args[1]);
        if (fbase && fexp) {
            return CTFEInterpValue{std::pow(*fbase, *fexp)};
        }
    }
    
    // sqrt() - square root
    if (name == "sqrt" && args.size() == 1) {
        if (auto f = toFloat(args[0])) {
            return CTFEInterpValue{std::sqrt(*f)};
        }
    }
    
    // floor() - floor function
    if (name == "floor" && args.size() == 1) {
        if (auto f = toFloat(args[0])) {
            return CTFEInterpValue{static_cast<int64_t>(std::floor(*f))};
        }
    }
    
    // ceil() - ceiling function
    if (name == "ceil" && args.size() == 1) {
        if (auto f = toFloat(args[0])) {
            return CTFEInterpValue{static_cast<int64_t>(std::ceil(*f))};
        }
    }
    
    // str() - convert to string
    if (name == "str" && args.size() == 1) {
        auto s = toString(args[0]);
        if (s) return CTFEInterpValue{*s};
    }
    
    // int() - convert to integer
    if (name == "int" && args.size() == 1) {
        if (auto* s = std::get_if<std::string>(&args[0])) {
            try {
                return CTFEInterpValue{static_cast<int64_t>(std::stoll(*s))};
            } catch (...) {
                return std::nullopt;
            }
        }
        auto i = toInt(args[0]);
        if (i) return CTFEInterpValue{*i};
    }

    // float() - convert to float
    if (name == "float" && args.size() == 1) {
        if (auto* s = std::get_if<std::string>(&args[0])) {
            try {
                return CTFEInterpValue{std::stod(*s)};
            } catch (...) {
                return std::nullopt;
            }
        }
        auto f = toFloat(args[0]);
        if (f) return CTFEInterpValue{*f};
    }
    
    // sizeof() - get size of type (simplified - returns common sizes)
    if (name == "sizeof" && args.size() == 1) {
        if (auto* s = std::get_if<std::string>(&args[0])) {
            if (*s == "int" || *s == "i64" || *s == "u64" || *s == "float" || *s == "f64") return CTFEInterpValue{int64_t(8)};
            if (*s == "i32" || *s == "u32" || *s == "f32") return CTFEInterpValue{int64_t(4)};
            if (*s == "i16" || *s == "u16") return CTFEInterpValue{int64_t(2)};
            if (*s == "i8" || *s == "u8" || *s == "bool") return CTFEInterpValue{int64_t(1)};
            // Check registered type metadata
            auto meta = getTypeMetadata(*s);
            if (meta) return CTFEInterpValue{static_cast<int64_t>(meta->size)};
        }
    }
    
    // alignof() - get alignment of type
    if (name == "alignof" && args.size() == 1) {
        if (auto* s = std::get_if<std::string>(&args[0])) {
            if (*s == "int" || *s == "i64" || *s == "u64" || *s == "float" || *s == "f64") return CTFEInterpValue{int64_t(8)};
            if (*s == "i32" || *s == "u32" || *s == "f32") return CTFEInterpValue{int64_t(4)};
            if (*s == "i16" || *s == "u16") return CTFEInterpValue{int64_t(2)};
            if (*s == "i8" || *s == "u8" || *s == "bool") return CTFEInterpValue{int64_t(1)};
            // Check registered type metadata
            auto meta = getTypeMetadata(*s);
            if (meta) return CTFEInterpValue{static_cast<int64_t>(meta->alignment)};
        }
    }
    
    // is_pod() - check if type is Plain Old Data (no custom destructor, trivially copyable)
    // POD types: primitives, records with only POD fields and no Drop trait
    if (name == "is_pod" && args.size() == 1) {
        if (auto* s = std::get_if<std::string>(&args[0])) {
            // Primitive types are always POD
            if (*s == "int" || *s == "i8" || *s == "i16" || *s == "i32" || *s == "i64" ||
                *s == "u8" || *s == "u16" || *s == "u32" || *s == "u64" ||
                *s == "float" || *s == "f16" || *s == "f32" || *s == "f64" || *s == "f128" ||
                *s == "bool" || *s == "char") {
                return CTFEInterpValue{true};
            }
            // Pointer types are POD
            if (s->find("ptr") != std::string::npos || s->find("*") != std::string::npos) {
                return CTFEInterpValue{true};
            }
            // String and list types are NOT POD (they have heap allocations)
            if (*s == "str" || s->find("[") != std::string::npos) {
                return CTFEInterpValue{false};
            }
            // Check registered type metadata for records
            auto meta = getTypeMetadata(*s);
            if (meta) {
                // A record is POD if all its fields are POD types
                // For now, assume records without Drop trait are POD
                // TODO: Check if type has Drop trait implementation
                bool isPod = true;
                for (const auto& field : meta->fields) {
                    // Recursively check if field type is POD
                    std::vector<CTFEInterpValue> fieldArgs = {CTFEInterpValue{field.typeName}};
                    auto fieldPod = evaluateBuiltin("is_pod", fieldArgs);
                    if (fieldPod) {
                        auto b = toBool(*fieldPod);
                        if (b && !*b) {
                            isPod = false;
                            break;
                        }
                    }
                }
                return CTFEInterpValue{isPod};
            }
            // Unknown type - assume not POD for safety
            return CTFEInterpValue{false};
        }
    }
    
    // is_trivially_copyable() - alias for is_pod
    if (name == "is_trivially_copyable" && args.size() == 1) {
        return evaluateBuiltin("is_pod", args);
    }
    
    // is_primitive() - check if type is a primitive type
    if (name == "is_primitive" && args.size() == 1) {
        if (auto* s = std::get_if<std::string>(&args[0])) {
            bool isPrimitive = (*s == "int" || *s == "i8" || *s == "i16" || *s == "i32" || *s == "i64" ||
                               *s == "u8" || *s == "u16" || *s == "u32" || *s == "u64" ||
                               *s == "float" || *s == "f16" || *s == "f32" || *s == "f64" || *s == "f128" ||
                               *s == "bool" || *s == "char" || *s == "nil");
            return CTFEInterpValue{isPrimitive};
        }
    }
    
    return std::nullopt;
}

// ============================================================================
// Compile-Time Reflection Implementation
// ============================================================================

void CTFEInterpreter::registerTypeMetadata(const std::string& typeName, const TypeMetadata& metadata) {
    typeMetadata_[typeName] = metadata;
}

const TypeMetadata* CTFEInterpreter::getTypeMetadata(const std::string& typeName) const {
    auto it = typeMetadata_.find(typeName);
    return it != typeMetadata_.end() ? &it->second : nullptr;
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateFieldsOf(const std::string& typeName) {
    auto meta = getTypeMetadata(typeName);
    if (!meta) return std::nullopt;
    
    // Return list of (name, type) tuples
    auto list = std::make_shared<CTFEInterpList>();
    for (const auto& field : meta->fields) {
        auto tuple = std::make_shared<CTFEInterpTuple>();
        tuple->elements.push_back(CTFEInterpValue{field.name});
        tuple->elements.push_back(CTFEInterpValue{field.typeName});
        list->elements.push_back(CTFEInterpValue{tuple});
    }
    return CTFEInterpValue{list};
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateMethodsOf(const std::string& typeName) {
    auto meta = getTypeMetadata(typeName);
    if (!meta) return std::nullopt;
    
    // Return list of method names
    auto list = std::make_shared<CTFEInterpList>();
    for (const auto& method : meta->methods) {
        list->elements.push_back(CTFEInterpValue{method.name});
    }
    return CTFEInterpValue{list};
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateTypeName(const std::string& typeName) {
    auto meta = getTypeMetadata(typeName);
    if (!meta) {
        // For primitive types, just return the type name
        return CTFEInterpValue{typeName};
    }
    return CTFEInterpValue{meta->name};
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateTypeSize(const std::string& typeName) {
    // Check primitive types first
    if (typeName == "int" || typeName == "i64" || typeName == "u64" || 
        typeName == "float" || typeName == "f64") return CTFEInterpValue{int64_t(8)};
    if (typeName == "i32" || typeName == "u32" || typeName == "f32") return CTFEInterpValue{int64_t(4)};
    if (typeName == "i16" || typeName == "u16") return CTFEInterpValue{int64_t(2)};
    if (typeName == "i8" || typeName == "u8" || typeName == "bool") return CTFEInterpValue{int64_t(1)};
    
    auto meta = getTypeMetadata(typeName);
    if (!meta) return std::nullopt;
    return CTFEInterpValue{static_cast<int64_t>(meta->size)};
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateTypeAlign(const std::string& typeName) {
    // Check primitive types first
    if (typeName == "int" || typeName == "i64" || typeName == "u64" || 
        typeName == "float" || typeName == "f64") return CTFEInterpValue{int64_t(8)};
    if (typeName == "i32" || typeName == "u32" || typeName == "f32") return CTFEInterpValue{int64_t(4)};
    if (typeName == "i16" || typeName == "u16") return CTFEInterpValue{int64_t(2)};
    if (typeName == "i8" || typeName == "u8" || typeName == "bool") return CTFEInterpValue{int64_t(1)};
    
    auto meta = getTypeMetadata(typeName);
    if (!meta) return std::nullopt;
    return CTFEInterpValue{static_cast<int64_t>(meta->alignment)};
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateHasField(const std::string& typeName, const std::string& fieldName) {
    auto meta = getTypeMetadata(typeName);
    if (!meta) return CTFEInterpValue{false};
    
    for (const auto& field : meta->fields) {
        if (field.name == fieldName) return CTFEInterpValue{true};
    }
    return CTFEInterpValue{false};
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateHasMethod(const std::string& typeName, const std::string& methodName) {
    auto meta = getTypeMetadata(typeName);
    if (!meta) return CTFEInterpValue{false};
    
    for (const auto& method : meta->methods) {
        if (method.name == methodName) return CTFEInterpValue{true};
    }
    return CTFEInterpValue{false};
}

std::optional<CTFEInterpValue> CTFEInterpreter::evaluateFieldType(const std::string& typeName, const std::string& fieldName) {
    auto meta = getTypeMetadata(typeName);
    if (!meta) return std::nullopt;
    
    for (const auto& field : meta->fields) {
        if (field.name == fieldName) return CTFEInterpValue{field.typeName};
    }
    return std::nullopt;
}

} // namespace tyl
