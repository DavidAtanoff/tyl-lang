// Tyl Compiler - Inter-Procedural Sparse Conditional Constant Propagation
// Propagates constants across function boundaries
#include "ipsccp.h"
#include <algorithm>
#include <iostream>

namespace tyl {

// ============================================
// LatticeValue Implementation
// ============================================

LatticeValue LatticeValue::meet(const LatticeValue& other) const {
    // Bottom meet anything = anything
    if (isBottom()) return other;
    if (other.isBottom()) return *this;
    
    // Top meet anything = Top
    if (isTop() || other.isTop()) return top();
    
    // Both are constants - check if they're equal
    if (type != other.type) return top();
    
    switch (type) {
        case Type::Int:
            if (intValue == other.intValue) return *this;
            return top();
        case Type::Float:
            if (floatValue == other.floatValue) return *this;
            return top();
        case Type::Bool:
            if (boolValue == other.boolValue) return *this;
            return top();
        case Type::String:
            if (stringValue == other.stringValue) return *this;
            return top();
        default:
            return top();
    }
}

bool LatticeValue::operator==(const LatticeValue& other) const {
    if (state != other.state) return false;
    if (state != State::Constant) return true;
    if (type != other.type) return false;
    
    switch (type) {
        case Type::Int: return intValue == other.intValue;
        case Type::Float: return floatValue == other.floatValue;
        case Type::Bool: return boolValue == other.boolValue;
        case Type::String: return stringValue == other.stringValue;
        default: return true;
    }
}

// ============================================
// IPSCCPPass Implementation
// ============================================

// Helper function to mark all variables modified in a loop body as top (non-constant)
void IPSCCPPass::markLoopModifiedVariablesAsTop(Block* body, const std::string& funcName) {
    if (!body) return;
    
    for (auto& stmt : body->statements) {
        if (auto* assign = dynamic_cast<AssignStmt*>(stmt.get())) {
            if (auto* ident = dynamic_cast<Identifier*>(assign->target.get())) {
                // Mark this variable as top since it's modified in a loop
                updateValue(funcName, ident->name, LatticeValue::top());
            }
        }
        else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            // Variables declared in a loop may have different values on each iteration
            updateValue(funcName, varDecl->name, LatticeValue::top());
        }
        else if (auto* nestedBlock = dynamic_cast<Block*>(stmt.get())) {
            markLoopModifiedVariablesAsTop(nestedBlock, funcName);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                markLoopModifiedVariablesAsTop(thenBlock, funcName);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    markLoopModifiedVariablesAsTop(elifBlock, funcName);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                markLoopModifiedVariablesAsTop(elseBlock, funcName);
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* whileBody = dynamic_cast<Block*>(whileLoop->body.get())) {
                markLoopModifiedVariablesAsTop(whileBody, funcName);
            }
        }
        else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt.get())) {
            updateValue(funcName, forLoop->var, LatticeValue::top());
            if (auto* forBody = dynamic_cast<Block*>(forLoop->body.get())) {
                markLoopModifiedVariablesAsTop(forBody, funcName);
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            // Check for assignment expressions (e.g., i = i + 1)
            if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (auto* ident = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                    updateValue(funcName, ident->name, LatticeValue::top());
                }
            }
        }
    }
}

void IPSCCPPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = IPSCCPStats{};
    functionSummaries_.clear();
    variableValues_.clear();
    worklist_.clear();
    
    // Phase 1: Collect functions and call sites
    collectFunctions(ast);
    collectCallSites(ast);
    
    // Phase 2: Run SCCP analysis
    runSCCP();
    
    // Phase 3: Apply transformations
    applyTransformations(ast);
}

void IPSCCPPass::collectFunctions(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            FunctionSummary summary;
            summary.decl = fn;
            summary.hasSideEffects = hasSideEffects(fn);
            
            // Initialize argument values to bottom
            for (size_t i = 0; i < fn->params.size(); ++i) {
                summary.argValues.push_back(LatticeValue::bottom());
            }
            summary.returnValue = LatticeValue::bottom();
            
            functionSummaries_[fn->name] = summary;
            
            // Initialize variable values for this function
            variableValues_[fn->name] = {};
            
            // Add to worklist
            worklist_.insert(fn->name);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* modFn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    FunctionSummary summary;
                    summary.decl = modFn;
                    summary.hasSideEffects = hasSideEffects(modFn);
                    
                    for (size_t i = 0; i < modFn->params.size(); ++i) {
                        summary.argValues.push_back(LatticeValue::bottom());
                    }
                    summary.returnValue = LatticeValue::bottom();
                    
                    std::string fullName = module->name + "::" + modFn->name;
                    functionSummaries_[fullName] = summary;
                    variableValues_[fullName] = {};
                    worklist_.insert(fullName);
                }
            }
        }
    }
}

void IPSCCPPass::collectCallSites(Program& ast) {
    // Collect all call sites to track argument values passed to each function
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (fn->body) {
                if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                    for (auto& s : block->statements) {
                        analyzeStatement(s.get(), fn->name);
                    }
                }
            }
        }
    }
}

void IPSCCPPass::runSCCP() {
    // Iterative worklist algorithm
    int iterations = 0;
    const int maxIterations = 100;
    
    while (!worklist_.empty() && iterations < maxIterations) {
        iterations++;
        
        auto it = worklist_.begin();
        std::string funcName = *it;
        worklist_.erase(it);
        
        analyzeFunction(funcName);
    }
}

void IPSCCPPass::analyzeFunction(const std::string& funcName) {
    auto it = functionSummaries_.find(funcName);
    if (it == functionSummaries_.end()) return;
    
    FunctionSummary& summary = it->second;
    if (!summary.decl || !summary.decl->body) return;
    
    // Initialize parameters with their lattice values
    for (size_t i = 0; i < summary.decl->params.size(); ++i) {
        const auto& param = summary.decl->params[i];
        variableValues_[funcName][param.first] = summary.argValues[i];
    }
    
    // Analyze function body
    if (auto* block = dynamic_cast<Block*>(summary.decl->body.get())) {
        for (auto& stmt : block->statements) {
            analyzeStatement(stmt.get(), funcName);
        }
    }
    
    summary.hasBeenAnalyzed = true;
}

void IPSCCPPass::analyzeStatement(Statement* stmt, const std::string& funcName) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            LatticeValue value = evaluateExpression(varDecl->initializer.get(), funcName);
            updateValue(funcName, varDecl->name, value);
        }
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* ident = dynamic_cast<Identifier*>(assign->target.get())) {
            LatticeValue value = evaluateExpression(assign->value.get(), funcName);
            // Meet with existing value (could be assigned in multiple paths)
            LatticeValue existing = getVariableValue(funcName, ident->name);
            updateValue(funcName, ident->name, existing.meet(value));
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            LatticeValue value = evaluateExpression(returnStmt->value.get(), funcName);
            auto& summary = functionSummaries_[funcName];
            LatticeValue newReturn = summary.returnValue.meet(value);
            if (newReturn != summary.returnValue) {
                summary.returnValue = newReturn;
                // Add callers to worklist
                for (const auto& caller : summary.calledFunctions) {
                    worklist_.insert(caller);
                }
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        // Evaluate condition
        LatticeValue cond = evaluateExpression(ifStmt->condition.get(), funcName);
        
        // If condition is constant, only analyze the taken branch
        if (cond.isConstant() && cond.type == LatticeValue::Type::Bool) {
            if (cond.boolValue) {
                if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                    for (auto& s : thenBlock->statements) {
                        analyzeStatement(s.get(), funcName);
                    }
                }
            } else {
                // Analyze elif branches
                for (auto& elif : ifStmt->elifBranches) {
                    LatticeValue elifCond = evaluateExpression(elif.first.get(), funcName);
                    if (elifCond.isConstant() && elifCond.type == LatticeValue::Type::Bool && elifCond.boolValue) {
                        if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                            for (auto& s : elifBlock->statements) {
                                analyzeStatement(s.get(), funcName);
                            }
                        }
                        return;
                    }
                }
                // Analyze else branch
                if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                    for (auto& s : elseBlock->statements) {
                        analyzeStatement(s.get(), funcName);
                    }
                }
            }
        } else {
            // Condition not constant - analyze all branches
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                for (auto& s : thenBlock->statements) {
                    analyzeStatement(s.get(), funcName);
                }
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    for (auto& s : elifBlock->statements) {
                        analyzeStatement(s.get(), funcName);
                    }
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                for (auto& s : elseBlock->statements) {
                    analyzeStatement(s.get(), funcName);
                }
            }
        }
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        // For while loops, variables modified in the loop body may have multiple values
        // We need to mark them as top (non-constant) before analyzing
        if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
            // First pass: find all variables modified in the loop and mark them as top
            markLoopModifiedVariablesAsTop(body, funcName);
            
            // Second pass: analyze the loop body
            for (auto& s : body->statements) {
                analyzeStatement(s.get(), funcName);
            }
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        // Loop variable is not constant
        updateValue(funcName, forLoop->var, LatticeValue::top());
        if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
            // First pass: find all variables modified in the loop and mark them as top
            markLoopModifiedVariablesAsTop(body, funcName);
            
            // Second pass: analyze the loop body
            for (auto& s : body->statements) {
                analyzeStatement(s.get(), funcName);
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        // Analyze for call expressions to update argument values
        if (auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
            if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                auto calleeIt = functionSummaries_.find(callee->name);
                if (calleeIt != functionSummaries_.end()) {
                    // Update argument values for the called function
                    for (size_t i = 0; i < call->args.size() && i < calleeIt->second.argValues.size(); ++i) {
                        LatticeValue argValue = evaluateExpression(call->args[i].get(), funcName);
                        LatticeValue newValue = calleeIt->second.argValues[i].meet(argValue);
                        if (newValue != calleeIt->second.argValues[i]) {
                            calleeIt->second.argValues[i] = newValue;
                            worklist_.insert(callee->name);
                        }
                    }
                    calleeIt->second.calledFunctions.insert(funcName);
                }
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            analyzeStatement(s.get(), funcName);
        }
    }
}

LatticeValue IPSCCPPass::evaluateExpression(Expression* expr, const std::string& funcName) {
    if (!expr) return LatticeValue::top();
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return LatticeValue::constant(intLit->value);
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return LatticeValue::constant(floatLit->value);
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return LatticeValue::constant(boolLit->value);
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return getVariableValue(funcName, ident->name);
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        LatticeValue lhs = evaluateExpression(binary->left.get(), funcName);
        LatticeValue rhs = evaluateExpression(binary->right.get(), funcName);
        return evaluateBinaryOp(binary->op, lhs, rhs);
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        LatticeValue operand = evaluateExpression(unary->operand.get(), funcName);
        return evaluateUnaryOp(unary->op, operand);
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Check if called function has constant return
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            auto it = functionSummaries_.find(callee->name);
            if (it != functionSummaries_.end() && it->second.returnValue.isConstant()) {
                return it->second.returnValue;
            }
        }
        return LatticeValue::top();
    }
    
    return LatticeValue::top();
}

LatticeValue IPSCCPPass::evaluateBinaryOp(TokenType op, const LatticeValue& lhs, const LatticeValue& rhs) {
    // If either is bottom, result is bottom
    if (lhs.isBottom() || rhs.isBottom()) return LatticeValue::bottom();
    
    // If either is top, result is top
    if (lhs.isTop() || rhs.isTop()) return LatticeValue::top();
    
    // Both are constants - evaluate
    if (lhs.type == LatticeValue::Type::Int && rhs.type == LatticeValue::Type::Int) {
        int64_t l = lhs.intValue;
        int64_t r = rhs.intValue;
        
        switch (op) {
            case TokenType::PLUS: return LatticeValue::constant(l + r);
            case TokenType::MINUS: return LatticeValue::constant(l - r);
            case TokenType::STAR: return LatticeValue::constant(l * r);
            case TokenType::SLASH: 
                if (r != 0) return LatticeValue::constant(l / r);
                return LatticeValue::top();
            case TokenType::PERCENT:
                if (r != 0) return LatticeValue::constant(l % r);
                return LatticeValue::top();
            case TokenType::AMP: return LatticeValue::constant(l & r);
            case TokenType::PIPE: return LatticeValue::constant(l | r);
            case TokenType::CARET: return LatticeValue::constant(l ^ r);
            case TokenType::EQ: return LatticeValue::constant(l == r);
            case TokenType::NE: return LatticeValue::constant(l != r);
            case TokenType::LT: return LatticeValue::constant(l < r);
            case TokenType::LE: return LatticeValue::constant(l <= r);
            case TokenType::GT: return LatticeValue::constant(l > r);
            case TokenType::GE: return LatticeValue::constant(l >= r);
            default: return LatticeValue::top();
        }
    }
    
    if (lhs.type == LatticeValue::Type::Bool && rhs.type == LatticeValue::Type::Bool) {
        bool l = lhs.boolValue;
        bool r = rhs.boolValue;
        
        switch (op) {
            case TokenType::AND: return LatticeValue::constant(l && r);
            case TokenType::OR: return LatticeValue::constant(l || r);
            case TokenType::EQ: return LatticeValue::constant(l == r);
            case TokenType::NE: return LatticeValue::constant(l != r);
            default: return LatticeValue::top();
        }
    }
    
    return LatticeValue::top();
}

LatticeValue IPSCCPPass::evaluateUnaryOp(TokenType op, const LatticeValue& operand) {
    if (operand.isBottom()) return LatticeValue::bottom();
    if (operand.isTop()) return LatticeValue::top();
    
    if (operand.type == LatticeValue::Type::Int) {
        switch (op) {
            case TokenType::MINUS: return LatticeValue::constant(-operand.intValue);
            case TokenType::TILDE: return LatticeValue::constant(~operand.intValue);
            default: return LatticeValue::top();
        }
    }
    
    if (operand.type == LatticeValue::Type::Bool) {
        switch (op) {
            case TokenType::NOT: return LatticeValue::constant(!operand.boolValue);
            default: return LatticeValue::top();
        }
    }
    
    return LatticeValue::top();
}

bool IPSCCPPass::updateValue(const std::string& funcName, const std::string& varName, const LatticeValue& value) {
    auto& funcVars = variableValues_[funcName];
    auto it = funcVars.find(varName);
    
    if (it == funcVars.end()) {
        funcVars[varName] = value;
        return true;
    }
    
    LatticeValue newValue = it->second.meet(value);
    if (newValue != it->second) {
        it->second = newValue;
        return true;
    }
    
    return false;
}

LatticeValue IPSCCPPass::getVariableValue(const std::string& funcName, const std::string& varName) {
    auto funcIt = variableValues_.find(funcName);
    if (funcIt == variableValues_.end()) return LatticeValue::top();
    
    auto varIt = funcIt->second.find(varName);
    if (varIt == funcIt->second.end()) return LatticeValue::top();
    
    return varIt->second;
}

void IPSCCPPass::applyTransformations(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            transformFunction(fn);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* modFn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    transformFunction(modFn);
                }
            }
        }
    }
}

void IPSCCPPass::transformFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
        transformStatements(block->statements, fn->name);
    }
}

void IPSCCPPass::transformStatements(std::vector<StmtPtr>& stmts, const std::string& funcName) {
    for (auto& stmt : stmts) {
        transformStatement(stmt, funcName);
    }
    
    // Remove dead code (statements after unconditional return, etc.)
    // This is handled by other passes
}

void IPSCCPPass::transformStatement(StmtPtr& stmt, const std::string& funcName) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto transformed = transformExpression(varDecl->initializer.get(), funcName);
            if (transformed) {
                varDecl->initializer = std::move(transformed);
                transformations_++;
                stats_.constantsFound++;
            }
        }
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt.get())) {
        if (assign->value) {
            auto transformed = transformExpression(assign->value.get(), funcName);
            if (transformed) {
                assign->value = std::move(transformed);
                transformations_++;
                stats_.constantsFound++;
            }
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            auto transformed = transformExpression(returnStmt->value.get(), funcName);
            if (transformed) {
                returnStmt->value = std::move(transformed);
                transformations_++;
                stats_.constantsFound++;
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        // Try to simplify condition
        LatticeValue cond = evaluateExpression(ifStmt->condition.get(), funcName);
        if (cond.isConstant() && cond.type == LatticeValue::Type::Bool) {
            stats_.branchesSimplified++;
            // Could replace entire if with just the taken branch
            // For now, just transform the branches
        }
        
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            transformStatements(thenBlock->statements, funcName);
        }
        for (auto& elif : ifStmt->elifBranches) {
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                transformStatements(elifBlock->statements, funcName);
            }
        }
        if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            transformStatements(elseBlock->statements, funcName);
        }
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt.get())) {
        if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
            transformStatements(body->statements, funcName);
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt.get())) {
        if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
            transformStatements(body->statements, funcName);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        if (exprStmt->expr) {
            auto transformed = transformExpression(exprStmt->expr.get(), funcName);
            if (transformed) {
                exprStmt->expr = std::move(transformed);
                transformations_++;
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        transformStatements(block->statements, funcName);
    }
}

ExprPtr IPSCCPPass::transformExpression(Expression* expr, const std::string& funcName) {
    if (!expr) return nullptr;
    
    // Check if this expression evaluates to a constant
    LatticeValue value = evaluateExpression(expr, funcName);
    if (value.isConstant()) {
        return createConstantExpr(value, expr->location);
    }
    
    // Try to replace call with constant return value
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto replaced = tryReplaceCallWithConstant(call);
        if (replaced) {
            stats_.callsSimplified++;
            return replaced;
        }
    }
    
    return nullptr;
}

ExprPtr IPSCCPPass::tryReplaceCallWithConstant(CallExpr* call) {
    if (!call) return nullptr;
    
    auto* callee = dynamic_cast<Identifier*>(call->callee.get());
    if (!callee) return nullptr;
    
    auto it = functionSummaries_.find(callee->name);
    if (it == functionSummaries_.end()) return nullptr;
    
    // Only replace if function has no side effects and returns constant
    if (it->second.hasSideEffects) return nullptr;
    if (!it->second.returnValue.isConstant()) return nullptr;
    
    return createConstantExpr(it->second.returnValue, call->location);
}

ExprPtr IPSCCPPass::createConstantExpr(const LatticeValue& value, const SourceLocation& loc) {
    switch (value.type) {
        case LatticeValue::Type::Int:
            return std::make_unique<IntegerLiteral>(value.intValue, loc);
        case LatticeValue::Type::Float:
            return std::make_unique<FloatLiteral>(value.floatValue, loc);
        case LatticeValue::Type::Bool:
            return std::make_unique<BoolLiteral>(value.boolValue, loc);
        default:
            return nullptr;
    }
}

bool IPSCCPPass::hasSideEffects(FnDecl* fn) {
    if (!fn || !fn->body) return true;  // Assume side effects if no body
    
    if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
        for (auto& stmt : block->statements) {
            if (hasSideEffectsInStmt(stmt.get())) return true;
        }
    }
    
    return false;
}

bool IPSCCPPass::hasSideEffectsInStmt(Statement* stmt) {
    if (!stmt) return false;
    
    // Check for print calls (they have side effects)
    // Note: In Tyl, print is a builtin function call, not a separate statement type
    
    // Expression statements may have side effects
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return hasSideEffectsInExpr(exprStmt->expr.get());
    }
    
    // Assignments to non-local variables have side effects
    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        // For now, assume all assignments are local
        return hasSideEffectsInExpr(assign->value.get());
    }
    
    // Check nested statements
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (hasSideEffectsInStmt(s.get())) return true;
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (hasSideEffectsInStmt(ifStmt->thenBranch.get())) return true;
        for (auto& elif : ifStmt->elifBranches) {
            if (hasSideEffectsInStmt(elif.second.get())) return true;
        }
        if (hasSideEffectsInStmt(ifStmt->elseBranch.get())) return true;
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        if (hasSideEffectsInStmt(whileLoop->body.get())) return true;
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        if (hasSideEffectsInStmt(forLoop->body.get())) return true;
    }
    
    return false;
}

bool IPSCCPPass::hasSideEffectsInExpr(Expression* expr) {
    if (!expr) return false;
    
    // Function calls may have side effects
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Check if called function has side effects
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            auto it = functionSummaries_.find(callee->name);
            if (it == functionSummaries_.end()) return true;  // Unknown function
            if (it->second.hasSideEffects) return true;
        } else {
            return true;  // Indirect call
        }
        
        // Check arguments
        for (auto& arg : call->args) {
            if (hasSideEffectsInExpr(arg.get())) return true;
        }
    }
    
    // Assignment expressions have side effects
    if (dynamic_cast<AssignExpr*>(expr)) return true;
    
    // Check subexpressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return hasSideEffectsInExpr(binary->left.get()) || 
               hasSideEffectsInExpr(binary->right.get());
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return hasSideEffectsInExpr(unary->operand.get());
    }
    
    return false;
}

} // namespace tyl
