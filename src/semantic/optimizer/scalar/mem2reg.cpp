// Tyl Compiler - mem2reg (Memory to Register Promotion) Implementation
// Promotes stack allocations to SSA registers when possible
#include "mem2reg.h"
#include <algorithm>

namespace tyl {

void Mem2RegPass::run(Program& ast) {
    transformations_ = 0;
    processStatements(ast.statements);
}

void Mem2RegPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fnDecl);
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            processStatements(block->statements);
        }
    }
}

void Mem2RegPass::processFunction(FnDecl* fn) {
    if (!fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (!body) return;
    
    // Reset state for this function
    promotableVars_.clear();
    currentVersion_.clear();
    versionStack_.clear();
    versionValues_.clear();
    blocks_.clear();
    
    // Phase 1: Find promotable variables
    findPromotableVars(body->statements);
    
    // Filter out non-promotable variables
    for (auto it = promotableVars_.begin(); it != promotableVars_.end(); ) {
        if (!it->second.isPromotable) {
            it = promotableVars_.erase(it);
        } else {
            ++it;
        }
    }
    
    if (promotableVars_.empty()) return;
    
    // Phase 2-4: For simple cases (no complex control flow), we can do
    // direct value propagation without full SSA construction
    
    // Initialize version tracking
    for (auto& [varName, info] : promotableVars_) {
        currentVersion_[varName] = 0;
        versionStack_[varName] = std::stack<int>();
        versionStack_[varName].push(0);
    }
    
    // Phase 5: Propagate values through the function
    propagateValues(body->statements);
    
    // Phase 6: Remove promoted allocations (declarations without uses)
    removePromotedAllocations(body->statements);
}

void Mem2RegPass::findPromotableVars(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            // Check if this is a simple type that can be promoted
            if (isSimpleType(varDecl->typeName)) {
                PromotableVar info;
                info.name = varDecl->name;
                info.typeName = varDecl->typeName;
                info.isPromotable = true;
                info.location = varDecl->location;
                info.defCount = varDecl->initializer ? 1 : 0;
                promotableVars_[varDecl->name] = info;
            }
        }
        
        // Analyze uses in all statements
        analyzeVarUse(stmt.get());
    }
}

void Mem2RegPass::analyzeVarUse(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            checkAddressTaken(varDecl->initializer.get());
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        // Count definitions
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            auto it = promotableVars_.find(ident->name);
            if (it != promotableVars_.end()) {
                it->second.defCount++;
            }
        }
        checkAddressTaken(assignStmt->value.get());
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        checkAddressTaken(exprStmt->expr.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            checkAddressTaken(returnStmt->value.get());
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            analyzeVarUse(s.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        checkAddressTaken(ifStmt->condition.get());
        analyzeVarUse(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            checkAddressTaken(elif.first.get());
            analyzeVarUse(elif.second.get());
        }
        analyzeVarUse(ifStmt->elseBranch.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        checkAddressTaken(forStmt->iterable.get());
        analyzeVarUse(forStmt->body.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        checkAddressTaken(whileStmt->condition.get());
        analyzeVarUse(whileStmt->body.get());
    }
}

void Mem2RegPass::checkAddressTaken(Expression* expr) {
    if (!expr) return;
    
    // Check for address-of operations
    if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr)) {
        if (auto* ident = dynamic_cast<Identifier*>(addrOf->operand.get())) {
            auto it = promotableVars_.find(ident->name);
            if (it != promotableVars_.end()) {
                it->second.hasAddressTaken = true;
                it->second.isPromotable = false;
            }
        }
    }
    else if (auto* borrow = dynamic_cast<BorrowExpr*>(expr)) {
        if (auto* ident = dynamic_cast<Identifier*>(borrow->operand.get())) {
            auto it = promotableVars_.find(ident->name);
            if (it != promotableVars_.end()) {
                it->second.hasAddressTaken = true;
                it->second.isPromotable = false;
            }
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        checkAddressTaken(binary->left.get());
        checkAddressTaken(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        checkAddressTaken(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        checkAddressTaken(call->callee.get());
        for (auto& arg : call->args) {
            checkAddressTaken(arg.get());
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        checkAddressTaken(index->object.get());
        checkAddressTaken(index->index.get());
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        checkAddressTaken(member->object.get());
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        checkAddressTaken(ternary->condition.get());
        checkAddressTaken(ternary->thenExpr.get());
        checkAddressTaken(ternary->elseExpr.get());
    }
    else if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        checkAddressTaken(walrus->value.get());
    }
}

bool Mem2RegPass::isSimpleType(const std::string& typeName) {
    // Simple types that can be promoted to registers
    static const std::set<std::string> simpleTypes = {
        "int", "i8", "i16", "i32", "i64",
        "uint", "u8", "u16", "u32", "u64",
        "float", "f32", "f64",
        "bool", "char"
    };
    return simpleTypes.count(typeName) > 0;
}

void Mem2RegPass::propagateValues(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        propagateInStatement(stmt);
    }
}

void Mem2RegPass::propagateInStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        auto it = promotableVars_.find(varDecl->name);
        if (it != promotableVars_.end()) {
            // Process initializer first
            if (varDecl->initializer) {
                varDecl->initializer = propagateInExpression(varDecl->initializer);
                
                // Store the value for this version
                int version = getNextVersion(varDecl->name);
                setVersionValue(varDecl->name, version, cloneExpr(varDecl->initializer.get()));
                pushVersion(varDecl->name, version);
            }
        } else if (varDecl->initializer) {
            varDecl->initializer = propagateInExpression(varDecl->initializer);
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        // Process value first
        assignStmt->value = propagateInExpression(assignStmt->value);
        
        // Check if target is a promotable variable
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            auto it = promotableVars_.find(ident->name);
            if (it != promotableVars_.end()) {
                // Store the new value
                int version = getNextVersion(ident->name);
                setVersionValue(ident->name, version, cloneExpr(assignStmt->value.get()));
                pushVersion(ident->name, version);
                transformations_++;
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        exprStmt->expr = propagateInExpression(exprStmt->expr);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            returnStmt->value = propagateInExpression(returnStmt->value);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        propagateValues(block->statements);
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        ifStmt->condition = propagateInExpression(ifStmt->condition);
        
        // Save current versions before branches
        auto savedVersions = currentVersion_;
        auto savedStacks = versionStack_;
        
        propagateInStatement(ifStmt->thenBranch);
        
        // Restore for else branch
        auto thenVersions = currentVersion_;
        currentVersion_ = savedVersions;
        versionStack_ = savedStacks;
        
        for (auto& elif : ifStmt->elifBranches) {
            elif.first = propagateInExpression(elif.first);
            propagateInStatement(elif.second);
        }
        
        if (ifStmt->elseBranch) {
            propagateInStatement(ifStmt->elseBranch);
        }
        
        // After if-else, we can't know which version is current
        // For now, invalidate versions modified in branches
        // (Full SSA would insert phi nodes here)
        for (auto& [varName, _] : promotableVars_) {
            if (thenVersions[varName] != savedVersions[varName] ||
                currentVersion_[varName] != savedVersions[varName]) {
                // Variable was modified in a branch - can't propagate past this
                currentVersion_[varName] = -1;  // Invalid version
            }
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        forStmt->iterable = propagateInExpression(forStmt->iterable);
        
        // Loop variables can't be propagated through loops
        // Invalidate all promotable vars that might be modified in loop
        auto savedVersions = currentVersion_;
        propagateInStatement(forStmt->body);
        
        // Invalidate vars modified in loop
        for (auto& [varName, _] : promotableVars_) {
            if (currentVersion_[varName] != savedVersions[varName]) {
                currentVersion_[varName] = -1;
            }
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        whileStmt->condition = propagateInExpression(whileStmt->condition);
        
        auto savedVersions = currentVersion_;
        propagateInStatement(whileStmt->body);
        
        for (auto& [varName, _] : promotableVars_) {
            if (currentVersion_[varName] != savedVersions[varName]) {
                currentVersion_[varName] = -1;
            }
        }
    }
}

ExprPtr Mem2RegPass::propagateInExpression(ExprPtr& expr) {
    if (!expr) return std::move(expr);
    
    // Check if this is a use of a promotable variable
    if (auto* ident = dynamic_cast<Identifier*>(expr.get())) {
        auto it = promotableVars_.find(ident->name);
        if (it != promotableVars_.end()) {
            int version = getCurrentVersion(ident->name);
            if (version >= 0) {
                ExprPtr value = getVersionValue(ident->name, version);
                if (value) {
                    // Replace with the known value
                    transformations_++;
                    return cloneExpr(value.get());
                }
            }
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        binary->left = propagateInExpression(binary->left);
        binary->right = propagateInExpression(binary->right);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        unary->operand = propagateInExpression(unary->operand);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        call->callee = propagateInExpression(call->callee);
        for (auto& arg : call->args) {
            arg = propagateInExpression(arg);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        index->object = propagateInExpression(index->object);
        index->index = propagateInExpression(index->index);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr.get())) {
        member->object = propagateInExpression(member->object);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        ternary->condition = propagateInExpression(ternary->condition);
        ternary->thenExpr = propagateInExpression(ternary->thenExpr);
        ternary->elseExpr = propagateInExpression(ternary->elseExpr);
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr.get())) {
        assign->value = propagateInExpression(assign->value);
        
        // Update version if target is promotable
        if (auto* ident = dynamic_cast<Identifier*>(assign->target.get())) {
            auto it = promotableVars_.find(ident->name);
            if (it != promotableVars_.end()) {
                int version = getNextVersion(ident->name);
                setVersionValue(ident->name, version, cloneExpr(assign->value.get()));
                pushVersion(ident->name, version);
            }
        }
    }
    
    return std::move(expr);
}

void Mem2RegPass::removePromotedAllocations(std::vector<StmtPtr>& stmts) {
    // Remove variable declarations for fully promoted variables
    // that are no longer needed
    std::vector<StmtPtr> newStmts;
    
    for (auto& stmt : stmts) {
        bool keep = true;
        
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            auto it = promotableVars_.find(varDecl->name);
            if (it != promotableVars_.end()) {
                // Keep the declaration but it's now just for type info
                // The actual value propagation has been done
                // In a full implementation, we might remove these entirely
                // if all uses have been replaced
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
            // Check if this is an assignment to a fully promoted variable
            // that has no remaining uses
            if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                auto it = promotableVars_.find(ident->name);
                if (it != promotableVars_.end()) {
                    // Keep assignments for now - DCE will clean up unused ones
                }
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            removePromotedAllocations(block->statements);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                removePromotedAllocations(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    removePromotedAllocations(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                removePromotedAllocations(elseBlock->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                removePromotedAllocations(body->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                removePromotedAllocations(body->statements);
            }
        }
        
        if (keep) {
            newStmts.push_back(std::move(stmt));
        }
    }
    
    stmts = std::move(newStmts);
}

int Mem2RegPass::getNextVersion(const std::string& varName) {
    return ++currentVersion_[varName];
}

int Mem2RegPass::getCurrentVersion(const std::string& varName) {
    auto it = currentVersion_.find(varName);
    if (it != currentVersion_.end()) {
        return it->second;
    }
    return -1;
}

void Mem2RegPass::pushVersion(const std::string& varName, int version) {
    versionStack_[varName].push(version);
}

void Mem2RegPass::popVersion(const std::string& varName) {
    if (!versionStack_[varName].empty()) {
        versionStack_[varName].pop();
    }
}

void Mem2RegPass::setVersionValue(const std::string& varName, int version, ExprPtr value) {
    versionValues_[varName][version] = std::move(value);
}

ExprPtr Mem2RegPass::getVersionValue(const std::string& varName, int version) {
    auto varIt = versionValues_.find(varName);
    if (varIt != versionValues_.end()) {
        auto verIt = varIt->second.find(version);
        if (verIt != varIt->second.end()) {
            return cloneExpr(verIt->second.get());
        }
    }
    return nullptr;
}

ExprPtr Mem2RegPass::cloneExpr(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(intLit->value, intLit->location, intLit->suffix);
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(floatLit->value, floatLit->location, floatLit->suffix);
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, boolLit->location);
    }
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(strLit->value, strLit->location);
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return std::make_unique<Identifier>(ident->name, ident->location);
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpr(binary->left.get()),
            binary->op,
            cloneExpr(binary->right.get()),
            binary->location
        );
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            cloneExpr(unary->operand.get()),
            unary->location
        );
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(cloneExpr(call->callee.get()), call->location);
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpr(arg.get()));
        }
        return newCall;
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpr(index->object.get()),
            cloneExpr(index->index.get()),
            index->location
        );
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpr(member->object.get()),
            member->member,
            member->location
        );
    }
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            cloneExpr(ternary->condition.get()),
            cloneExpr(ternary->thenExpr.get()),
            cloneExpr(ternary->elseExpr.get()),
            ternary->location
        );
    }
    
    return nullptr;
}

std::unique_ptr<Mem2RegPass> createMem2RegPass() {
    return std::make_unique<Mem2RegPass>();
}

} // namespace tyl
