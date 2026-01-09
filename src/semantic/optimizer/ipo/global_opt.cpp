// Tyl Compiler - Global Optimization Pass Implementation
// Based on LLVM's GlobalOpt pass
#include "global_opt.h"
#include <iostream>

namespace tyl {

void GlobalOptPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = GlobalOptStats{};
    globals_.clear();
    functions_.clear();
    
    // Phase 1: Collect globals and functions
    collectGlobals(ast);
    collectFunctions(ast);
    
    if (globals_.empty()) return;
    
    // Phase 2: Analyze usage
    analyzeGlobalUsage(ast);
    
    // Determine which optimizations apply
    determineOptimizations();
    
    // Phase 3: Apply optimizations
    applyOptimizations(ast);
    
    transformations_ = stats_.globalsConstified + stats_.globalsEliminated +
                       stats_.loadsReplaced + stats_.storesEliminated;
}

void GlobalOptPass::collectGlobals(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            // Top-level variable declarations are globals
            GlobalVarInfo info;
            info.decl = varDecl;
            info.name = varDecl->name;
            // Top-level variables are considered exported (public) by default
            info.isExported = true;
            
            // Check if it has a constant initializer
            if (varDecl->initializer) {
                info.hasConstantInit = isConstantExpr(varDecl->initializer.get());
                if (info.hasConstantInit) {
                    evaluateConstant(varDecl->initializer.get(), info);
                }
            }
            
            // Check if declared as const
            if (varDecl->isConst) {
                info.canConstify = false; // Already const
            }
            
            globals_[varDecl->name] = info;
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            // Collect globals from modules
            for (auto& modStmt : module->body) {
                if (auto* varDecl = dynamic_cast<VarDecl*>(modStmt.get())) {
                    GlobalVarInfo info;
                    info.decl = varDecl;
                    info.name = module->name + "::" + varDecl->name;
                    // Module-level variables are exported if the module is public
                    info.isExported = module->isPublic;
                    
                    if (varDecl->initializer) {
                        info.hasConstantInit = isConstantExpr(varDecl->initializer.get());
                        if (info.hasConstantInit) {
                            evaluateConstant(varDecl->initializer.get(), info);
                        }
                    }
                    
                    globals_[info.name] = info;
                }
            }
        }
    }
}

void GlobalOptPass::collectFunctions(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            functions_.insert(fn->name);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    functions_.insert(module->name + "::" + fn->name);
                }
            }
        }
    }
}

void GlobalOptPass::analyzeGlobalUsage(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            analyzeUsageInFunction(fn);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    analyzeUsageInFunction(fn);
                }
            }
        }
    }
}

void GlobalOptPass::analyzeUsageInFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    analyzeUsageInStmt(fn->body.get(), fn->name);
}

void GlobalOptPass::analyzeUsageInStmt(Statement* stmt, const std::string& funcName) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        analyzeUsageInExpr(exprStmt->expr.get(), funcName);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            analyzeUsageInExpr(varDecl->initializer.get(), funcName);
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        // Check if assigning to a global
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            if (globals_.count(ident->name)) {
                globals_[ident->name].isWritten = true;
                globals_[ident->name].writtenInFunctions.insert(funcName);
            }
        }
        analyzeUsageInExpr(assignStmt->target.get(), funcName, true);
        analyzeUsageInExpr(assignStmt->value.get(), funcName);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            analyzeUsageInExpr(returnStmt->value.get(), funcName);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        analyzeUsageInExpr(ifStmt->condition.get(), funcName);
        analyzeUsageInStmt(ifStmt->thenBranch.get(), funcName);
        for (auto& elif : ifStmt->elifBranches) {
            analyzeUsageInExpr(elif.first.get(), funcName);
            analyzeUsageInStmt(elif.second.get(), funcName);
        }
        analyzeUsageInStmt(ifStmt->elseBranch.get(), funcName);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        analyzeUsageInExpr(whileStmt->condition.get(), funcName);
        analyzeUsageInStmt(whileStmt->body.get(), funcName);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        analyzeUsageInExpr(forStmt->iterable.get(), funcName);
        analyzeUsageInStmt(forStmt->body.get(), funcName);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            analyzeUsageInStmt(s.get(), funcName);
        }
    }
}

void GlobalOptPass::analyzeUsageInExpr(Expression* expr, const std::string& funcName, bool isWrite) {
    if (!expr) return;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (globals_.count(ident->name)) {
            if (isWrite) {
                globals_[ident->name].isWritten = true;
                globals_[ident->name].writtenInFunctions.insert(funcName);
            } else {
                globals_[ident->name].isRead = true;
                globals_[ident->name].readInFunctions.insert(funcName);
            }
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        analyzeUsageInExpr(binary->left.get(), funcName);
        analyzeUsageInExpr(binary->right.get(), funcName);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        analyzeUsageInExpr(unary->operand.get(), funcName);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        analyzeUsageInExpr(call->callee.get(), funcName);
        for (auto& arg : call->args) {
            analyzeUsageInExpr(arg.get(), funcName);
        }
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        analyzeUsageInExpr(member->object.get(), funcName);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        analyzeUsageInExpr(index->object.get(), funcName);
        analyzeUsageInExpr(index->index.get(), funcName);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        analyzeUsageInExpr(ternary->condition.get(), funcName);
        analyzeUsageInExpr(ternary->thenExpr.get(), funcName);
        analyzeUsageInExpr(ternary->elseExpr.get(), funcName);
    }
    else if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr)) {
        // Address taken - can't optimize
        if (auto* ident = dynamic_cast<Identifier*>(addrOf->operand.get())) {
            if (globals_.count(ident->name)) {
                globals_[ident->name].isAddressTaken = true;
            }
        }
        analyzeUsageInExpr(addrOf->operand.get(), funcName);
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        analyzeUsageInExpr(assign->target.get(), funcName, true);
        analyzeUsageInExpr(assign->value.get(), funcName);
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            analyzeUsageInExpr(elem.get(), funcName);
        }
    }
    else if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        for (auto& field : record->fields) {
            analyzeUsageInExpr(field.second.get(), funcName);
        }
    }
}

bool GlobalOptPass::isConstantExpr(Expression* expr) {
    if (!expr) return false;
    
    if (dynamic_cast<IntegerLiteral*>(expr)) return true;
    if (dynamic_cast<FloatLiteral*>(expr)) return true;
    if (dynamic_cast<BoolLiteral*>(expr)) return true;
    if (dynamic_cast<StringLiteral*>(expr)) return true;
    if (dynamic_cast<NilLiteral*>(expr)) return true;
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isConstantExpr(unary->operand.get());
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isConstantExpr(binary->left.get()) && isConstantExpr(binary->right.get());
    }
    
    return false;
}

bool GlobalOptPass::evaluateConstant(Expression* expr, GlobalVarInfo& info) {
    if (!expr) return false;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        info.constType = GlobalVarInfo::ConstType::Int;
        info.constantIntValue = intLit->value;
        return true;
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        info.constType = GlobalVarInfo::ConstType::Float;
        info.constantFloatValue = floatLit->value;
        return true;
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        info.constType = GlobalVarInfo::ConstType::Bool;
        info.constantBoolValue = boolLit->value;
        return true;
    }
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        info.constType = GlobalVarInfo::ConstType::String;
        info.constantStrValue = strLit->value;
        return true;
    }
    
    // Handle unary negation
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        if (unary->op == TokenType::MINUS) {
            if (auto* intLit = dynamic_cast<IntegerLiteral*>(unary->operand.get())) {
                info.constType = GlobalVarInfo::ConstType::Int;
                info.constantIntValue = -intLit->value;
                return true;
            }
            if (auto* floatLit = dynamic_cast<FloatLiteral*>(unary->operand.get())) {
                info.constType = GlobalVarInfo::ConstType::Float;
                info.constantFloatValue = -floatLit->value;
                return true;
            }
        }
    }
    
    return false;
}

void GlobalOptPass::determineOptimizations() {
    for (auto& [name, info] : globals_) {
        // Skip if address is taken - can't safely optimize
        if (info.isAddressTaken) continue;
        
        // Can eliminate if never read
        if (!info.isRead && !info.isExported) {
            info.canEliminate = true;
            continue;
        }
        
        // Can constify if never written after initialization and has constant init
        if (!info.isWritten && info.hasConstantInit && !info.decl->isConst) {
            info.canConstify = true;
        }
    }
}

void GlobalOptPass::applyOptimizations(Program& ast) {
    // First, replace constant loads (before eliminating globals)
    replaceConstantLoads(ast);
    
    // Then eliminate unused globals
    eliminateUnusedGlobals(ast);
}

void GlobalOptPass::eliminateUnusedGlobals(Program& ast) {
    // Remove unused globals from top level
    auto it = ast.statements.begin();
    while (it != ast.statements.end()) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(it->get())) {
            if (globals_.count(varDecl->name) && globals_[varDecl->name].canEliminate) {
                it = ast.statements.erase(it);
                stats_.globalsEliminated++;
                continue;
            }
        }
        ++it;
    }
    
    // Remove from modules
    for (auto& stmt : ast.statements) {
        if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            auto modIt = module->body.begin();
            while (modIt != module->body.end()) {
                if (auto* varDecl = dynamic_cast<VarDecl*>(modIt->get())) {
                    std::string fullName = module->name + "::" + varDecl->name;
                    if (globals_.count(fullName) && globals_[fullName].canEliminate) {
                        modIt = module->body.erase(modIt);
                        stats_.globalsEliminated++;
                        continue;
                    }
                }
                ++modIt;
            }
        }
    }
}

void GlobalOptPass::replaceConstantLoads(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (fn->body) {
                replaceConstantLoadsInStmt(fn->body.get());
            }
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    if (fn->body) {
                        replaceConstantLoadsInStmt(fn->body.get());
                    }
                }
            }
        }
    }
}

void GlobalOptPass::replaceConstantLoadsInStmt(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        replaceConstantLoadsInExpr(exprStmt->expr);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            replaceConstantLoadsInExpr(varDecl->initializer);
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        replaceConstantLoadsInExpr(assignStmt->value);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            replaceConstantLoadsInExpr(returnStmt->value);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        replaceConstantLoadsInExpr(ifStmt->condition);
        replaceConstantLoadsInStmt(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            replaceConstantLoadsInExpr(elif.first);
            replaceConstantLoadsInStmt(elif.second.get());
        }
        replaceConstantLoadsInStmt(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        replaceConstantLoadsInExpr(whileStmt->condition);
        replaceConstantLoadsInStmt(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        replaceConstantLoadsInExpr(forStmt->iterable);
        replaceConstantLoadsInStmt(forStmt->body.get());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            replaceConstantLoadsInStmt(s.get());
        }
    }
}

void GlobalOptPass::replaceConstantLoadsInExpr(ExprPtr& expr) {
    if (!expr) return;
    
    // Check if this is a read from a constifiable global
    if (auto* ident = dynamic_cast<Identifier*>(expr.get())) {
        if (globals_.count(ident->name)) {
            auto& info = globals_[ident->name];
            if (info.canConstify && info.constType != GlobalVarInfo::ConstType::None) {
                auto constExpr = createConstantExpr(info, ident->location);
                if (constExpr) {
                    expr = std::move(constExpr);
                    stats_.loadsReplaced++;
                    stats_.globalsConstified++;
                    return;
                }
            }
        }
    }
    
    // Recurse into subexpressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        replaceConstantLoadsInExpr(binary->left);
        replaceConstantLoadsInExpr(binary->right);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        replaceConstantLoadsInExpr(unary->operand);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            replaceConstantLoadsInExpr(arg);
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        replaceConstantLoadsInExpr(ternary->condition);
        replaceConstantLoadsInExpr(ternary->thenExpr);
        replaceConstantLoadsInExpr(ternary->elseExpr);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        replaceConstantLoadsInExpr(index->object);
        replaceConstantLoadsInExpr(index->index);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr.get())) {
        replaceConstantLoadsInExpr(member->object);
    }
}

ExprPtr GlobalOptPass::createConstantExpr(const GlobalVarInfo& info, SourceLocation loc) {
    switch (info.constType) {
        case GlobalVarInfo::ConstType::Int:
            return std::make_unique<IntegerLiteral>(info.constantIntValue, loc);
        case GlobalVarInfo::ConstType::Float:
            return std::make_unique<FloatLiteral>(info.constantFloatValue, loc);
        case GlobalVarInfo::ConstType::Bool:
            return std::make_unique<BoolLiteral>(info.constantBoolValue, loc);
        case GlobalVarInfo::ConstType::String:
            return std::make_unique<StringLiteral>(info.constantStrValue, loc);
        default:
            return nullptr;
    }
}

} // namespace tyl
