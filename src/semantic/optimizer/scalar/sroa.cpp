// Tyl Compiler - SROA (Scalar Replacement of Aggregates) Implementation
// Breaks up aggregates (records/structs) into individual scalar variables
#include "sroa.h"
#include <algorithm>

namespace tyl {

void SROAPass::run(Program& ast) {
    transformations_ = 0;
    
    // First, collect all record type definitions
    collectRecordTypes(ast);
    
    // Process all statements
    processStatements(ast.statements);
}

void SROAPass::collectRecordTypes(Program& ast) {
    recordTypes_.clear();
    
    for (auto& stmt : ast.statements) {
        if (auto* recordDecl = dynamic_cast<RecordDecl*>(stmt.get())) {
            std::vector<std::pair<std::string, std::string>> fields;
            for (const auto& field : recordDecl->fields) {
                fields.push_back({field.first, field.second});
            }
            recordTypes_[recordDecl->name] = fields;
        }
    }
}

void SROAPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fnDecl);
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            processStatements(block->statements);
        }
    }
}

void SROAPass::processFunction(FnDecl* fn) {
    if (!fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (!body) return;
    
    // Reset state for this function
    candidates_.clear();
    scalarReplacements_.clear();
    
    // Phase 1: Find candidate record variables
    findCandidates(body->statements);
    
    // Phase 2: Check which candidates can actually be split
    // (no address taken, no whole-record uses except initialization)
    for (auto& [varName, candidate] : candidates_) {
        checkCandidate(fn->body.get(), varName);
    }
    
    // Phase 3: Create scalar replacements for valid candidates
    createScalarReplacements(body->statements);
    
    // Phase 4: Rewrite field accesses to use scalar variables
    rewriteAccesses(body->statements);
}

void SROAPass::findCandidates(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            // Check if this is a record type variable
            if (isRecordType(varDecl->typeName)) {
                SROACandidate candidate;
                candidate.varName = varDecl->name;
                candidate.typeName = varDecl->typeName;
                candidate.fields = getRecordFields(varDecl->typeName);
                candidate.canSplit = !candidate.fields.empty();
                candidate.location = varDecl->location;
                candidates_[varDecl->name] = candidate;
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            findCandidates(block->statements);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                findCandidates(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    findCandidates(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                findCandidates(elseBlock->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                findCandidates(body->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                findCandidates(body->statements);
            }
        }
    }
}

void SROAPass::checkCandidate(Statement* stmt, const std::string& varName) {
    if (!stmt) return;
    
    auto it = candidates_.find(varName);
    if (it == candidates_.end() || !it->second.canSplit) return;
    
    // Check for address-taken or whole-record uses
    std::function<void(Statement*)> checkStmt = [&](Statement* s) {
        if (!s) return;
        
        if (auto* varDecl = dynamic_cast<VarDecl*>(s)) {
            if (varDecl->initializer) {
                if (isAddressTaken(varDecl->initializer.get(), varName)) {
                    it->second.canSplit = false;
                }
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(s)) {
            // Check if assigning whole record (not field)
            if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                if (ident->name == varName) {
                    // Whole record assignment - can't split unless it's a RecordExpr
                    if (!dynamic_cast<RecordExpr*>(assignStmt->value.get())) {
                        it->second.canSplit = false;
                    }
                }
            }
            if (isAddressTaken(assignStmt->value.get(), varName)) {
                it->second.canSplit = false;
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(s)) {
            if (isAddressTaken(exprStmt->expr.get(), varName) ||
                isWholeRecordUse(exprStmt->expr.get(), varName)) {
                it->second.canSplit = false;
            }
        }
        else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(s)) {
            if (returnStmt->value) {
                // Returning whole record - can't split
                if (auto* ident = dynamic_cast<Identifier*>(returnStmt->value.get())) {
                    if (ident->name == varName) {
                        it->second.canSplit = false;
                    }
                }
                if (isAddressTaken(returnStmt->value.get(), varName)) {
                    it->second.canSplit = false;
                }
            }
        }
        else if (auto* block = dynamic_cast<Block*>(s)) {
            for (auto& sub : block->statements) {
                checkStmt(sub.get());
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(s)) {
            if (isWholeRecordUse(ifStmt->condition.get(), varName)) {
                it->second.canSplit = false;
            }
            checkStmt(ifStmt->thenBranch.get());
            for (auto& elif : ifStmt->elifBranches) {
                checkStmt(elif.second.get());
            }
            checkStmt(ifStmt->elseBranch.get());
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(s)) {
            checkStmt(forStmt->body.get());
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(s)) {
            if (isWholeRecordUse(whileStmt->condition.get(), varName)) {
                it->second.canSplit = false;
            }
            checkStmt(whileStmt->body.get());
        }
    };
    
    checkStmt(stmt);
}

bool SROAPass::isAddressTaken(Expression* expr, const std::string& varName) {
    if (!expr) return false;
    
    if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr)) {
        if (auto* ident = dynamic_cast<Identifier*>(addrOf->operand.get())) {
            if (ident->name == varName) return true;
        }
        if (auto* member = dynamic_cast<MemberExpr*>(addrOf->operand.get())) {
            if (auto* obj = dynamic_cast<Identifier*>(member->object.get())) {
                if (obj->name == varName) return true;
            }
        }
    }
    else if (auto* borrow = dynamic_cast<BorrowExpr*>(expr)) {
        if (auto* ident = dynamic_cast<Identifier*>(borrow->operand.get())) {
            if (ident->name == varName) return true;
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isAddressTaken(binary->left.get(), varName) ||
               isAddressTaken(binary->right.get(), varName);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            if (isAddressTaken(arg.get(), varName)) return true;
            // Also check if whole record is passed to function
            if (auto* ident = dynamic_cast<Identifier*>(arg.get())) {
                if (ident->name == varName) return true;
            }
        }
    }
    
    return false;
}

bool SROAPass::isWholeRecordUse(Expression* expr, const std::string& varName) {
    if (!expr) return false;
    
    // Direct use of the variable (not a field access)
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return ident->name == varName;
    }
    
    // Check in binary expressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isWholeRecordUse(binary->left.get(), varName) ||
               isWholeRecordUse(binary->right.get(), varName);
    }
    
    // Check in function calls
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            if (isWholeRecordUse(arg.get(), varName)) return true;
        }
    }
    
    return false;
}

void SROAPass::createScalarReplacements(std::vector<StmtPtr>& stmts) {
    std::vector<StmtPtr> newStmts;
    
    for (auto& stmt : stmts) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            auto it = candidates_.find(varDecl->name);
            if (it != candidates_.end() && it->second.canSplit) {
                // Create scalar variables for each field
                const auto& candidate = it->second;
                
                for (const auto& field : candidate.fields) {
                    std::string scalarName = makeScalarName(varDecl->name, field.first);
                    scalarReplacements_[varDecl->name][field.first] = scalarName;
                    
                    // Extract initializer from RecordExpr if present
                    ExprPtr fieldInit = nullptr;
                    if (auto* recordExpr = dynamic_cast<RecordExpr*>(varDecl->initializer.get())) {
                        for (const auto& initField : recordExpr->fields) {
                            if (initField.first == field.first) {
                                fieldInit = cloneExpr(initField.second.get());
                                break;
                            }
                        }
                    }
                    
                    // Create scalar variable declaration
                    auto scalarDecl = std::make_unique<VarDecl>(
                        scalarName, field.second, std::move(fieldInit), varDecl->location);
                    scalarDecl->isMutable = varDecl->isMutable;
                    
                    newStmts.push_back(std::move(scalarDecl));
                }
                
                transformations_++;
                continue;  // Don't add original declaration
            }
        }
        
        newStmts.push_back(std::move(stmt));
    }
    
    stmts = std::move(newStmts);
}

void SROAPass::rewriteAccesses(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        rewriteStatement(stmt);
    }
}

void SROAPass::rewriteStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        // Check if target is a field access on a split record
        if (auto* member = dynamic_cast<MemberExpr*>(assignStmt->target.get())) {
            if (auto* obj = dynamic_cast<Identifier*>(member->object.get())) {
                auto it = scalarReplacements_.find(obj->name);
                if (it != scalarReplacements_.end()) {
                    auto fieldIt = it->second.find(member->member);
                    if (fieldIt != it->second.end()) {
                        // Replace with scalar assignment
                        assignStmt->target = std::make_unique<Identifier>(
                            fieldIt->second, member->location);
                        transformations_++;
                    }
                }
            }
        }
        
        // Rewrite value expression
        assignStmt->value = rewriteExpression(assignStmt->value);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            varDecl->initializer = rewriteExpression(varDecl->initializer);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        exprStmt->expr = rewriteExpression(exprStmt->expr);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            returnStmt->value = rewriteExpression(returnStmt->value);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        createScalarReplacements(block->statements);
        rewriteAccesses(block->statements);
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        ifStmt->condition = rewriteExpression(ifStmt->condition);
        rewriteStatement(ifStmt->thenBranch);
        for (auto& elif : ifStmt->elifBranches) {
            elif.first = rewriteExpression(elif.first);
            rewriteStatement(elif.second);
        }
        if (ifStmt->elseBranch) {
            rewriteStatement(ifStmt->elseBranch);
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        forStmt->iterable = rewriteExpression(forStmt->iterable);
        rewriteStatement(forStmt->body);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        whileStmt->condition = rewriteExpression(whileStmt->condition);
        rewriteStatement(whileStmt->body);
    }
}

ExprPtr SROAPass::rewriteExpression(ExprPtr& expr) {
    if (!expr) return std::move(expr);
    
    // Check for field access on split record
    if (auto* member = dynamic_cast<MemberExpr*>(expr.get())) {
        if (auto* obj = dynamic_cast<Identifier*>(member->object.get())) {
            auto it = scalarReplacements_.find(obj->name);
            if (it != scalarReplacements_.end()) {
                auto fieldIt = it->second.find(member->member);
                if (fieldIt != it->second.end()) {
                    // Replace with scalar variable reference
                    transformations_++;
                    return std::make_unique<Identifier>(fieldIt->second, member->location);
                }
            }
        }
        // Recursively process object
        member->object = rewriteExpression(member->object);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        binary->left = rewriteExpression(binary->left);
        binary->right = rewriteExpression(binary->right);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        unary->operand = rewriteExpression(unary->operand);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        call->callee = rewriteExpression(call->callee);
        for (auto& arg : call->args) {
            arg = rewriteExpression(arg);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        index->object = rewriteExpression(index->object);
        index->index = rewriteExpression(index->index);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        ternary->condition = rewriteExpression(ternary->condition);
        ternary->thenExpr = rewriteExpression(ternary->thenExpr);
        ternary->elseExpr = rewriteExpression(ternary->elseExpr);
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr.get())) {
        assign->target = rewriteExpression(assign->target);
        assign->value = rewriteExpression(assign->value);
    }
    else if (auto* walrus = dynamic_cast<WalrusExpr*>(expr.get())) {
        walrus->value = rewriteExpression(walrus->value);
    }
    
    return std::move(expr);
}

bool SROAPass::isRecordType(const std::string& typeName) {
    return recordTypes_.find(typeName) != recordTypes_.end();
}

std::vector<std::pair<std::string, std::string>> SROAPass::getRecordFields(const std::string& typeName) {
    auto it = recordTypes_.find(typeName);
    if (it != recordTypes_.end()) {
        return it->second;
    }
    return {};
}

std::string SROAPass::makeScalarName(const std::string& varName, const std::string& fieldName) {
    return varName + "_" + fieldName + "_sroa";
}

ExprPtr SROAPass::cloneExpr(Expression* expr) {
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
    if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        return std::make_unique<WalrusExpr>(
            walrus->varName,
            cloneExpr(walrus->value.get()),
            walrus->location);
    }
    
    return nullptr;
}

std::unique_ptr<SROAPass> createSROAPass() {
    return std::make_unique<SROAPass>();
}

} // namespace tyl
