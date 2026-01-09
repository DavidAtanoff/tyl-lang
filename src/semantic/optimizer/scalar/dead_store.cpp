// Tyl Compiler - Dead Store Elimination Implementation
// Removes stores to variables that are overwritten before being read
#include "dead_store.h"
#include <algorithm>

namespace tyl {

void DeadStoreEliminationPass::run(Program& ast) {
    transformations_ = 0;
    
    // Process top-level statements
    processNestedStructures(ast.statements);
    processBlock(ast.statements);
}

void DeadStoreEliminationPass::processNestedStructures(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                processNestedStructures(body->statements);
                processBlock(body->statements);
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            processNestedStructures(block->statements);
            processBlock(block->statements);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                processNestedStructures(thenBlock->statements);
                processBlock(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    processNestedStructures(elifBlock->statements);
                    processBlock(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processNestedStructures(elseBlock->statements);
                processBlock(elseBlock->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            // DON'T process loop bodies for dead store elimination!
            // Variables modified in a loop body may be read in subsequent iterations.
            // Only recurse into nested structures within the loop body.
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                processNestedStructures(body->statements);
                // NOTE: We intentionally do NOT call processBlock on loop bodies
                // because DSE cannot safely determine if a store is dead when
                // the variable might be read in the next iteration.
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            // Same as for loops - don't process while loop bodies for DSE
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                processNestedStructures(body->statements);
                // NOTE: We intentionally do NOT call processBlock on loop bodies
            }
        }
    }
}

void DeadStoreEliminationPass::processBlock(std::vector<StmtPtr>& statements) {
    if (statements.empty()) return;
    
    // Analyze all stores in this block
    std::vector<StoreInfo> stores;
    analyzeStores(statements, stores);
    
    if (stores.empty()) return;
    
    // Build a map of variable -> list of store indices
    std::unordered_map<std::string, std::vector<size_t>> varToStores;
    for (size_t i = 0; i < stores.size(); ++i) {
        varToStores[stores[i].varName].push_back(i);
    }
    
    // For each variable with multiple stores, check for dead stores
    for (auto& [varName, storeIndices] : varToStores) {
        if (storeIndices.size() < 2) continue;
        
        // Check each store except the last one
        for (size_t i = 0; i < storeIndices.size() - 1; ++i) {
            size_t storeIdx = storeIndices[i];
            size_t nextStoreIdx = storeIndices[i + 1];
            
            StoreInfo& store = stores[storeIdx];
            StoreInfo& nextStore = stores[nextStoreIdx];
            
            // Skip if already marked dead
            if (store.isDead) continue;
            
            // Skip declarations - they define the variable
            if (store.isDeclaration) continue;
            
            // Check if variable is read between this store and the next
            if (!isReadBetween(statements, varName, store.index, nextStore.index)) {
                // Check if variable escapes (passed to function, etc.)
                if (!variableEscapes(statements, varName, store.index)) {
                    store.isDead = true;
                    transformations_++;
                }
            }
        }
        
        // Check if the last store is dead (not read after)
        size_t lastStoreIdx = storeIndices.back();
        StoreInfo& lastStore = stores[lastStoreIdx];
        
        if (!lastStore.isDeclaration && !lastStore.isDead) {
            if (!isReadAfter(statements, varName, lastStore.index)) {
                if (!variableEscapes(statements, varName, lastStore.index)) {
                    lastStore.isDead = true;
                    transformations_++;
                }
            }
        }
    }
    
    // Remove dead stores
    removeDeadStores(statements, stores);
}

void DeadStoreEliminationPass::analyzeStores(std::vector<StmtPtr>& statements,
                                              std::vector<StoreInfo>& stores) {
    for (size_t i = 0; i < statements.size(); ++i) {
        auto* stmt = statements[i].get();
        
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            if (varDecl->initializer) {
                StoreInfo info;
                info.index = i;
                info.varName = varDecl->name;
                info.isDeclaration = true;
                info.isDead = false;
                collectReads(varDecl->initializer.get(), info.readsInValue);
                stores.push_back(info);
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                StoreInfo info;
                info.index = i;
                info.varName = target->name;
                info.isDeclaration = false;
                info.isDead = false;
                collectReads(assignStmt->value.get(), info.readsInValue);
                
                // For compound assignments (+=, -=, etc.), the target is also read
                if (assignStmt->op != TokenType::ASSIGN) {
                    info.readsInValue.insert(target->name);
                }
                
                stores.push_back(info);
            }
        }
    }
}

void DeadStoreEliminationPass::collectReads(Expression* expr, std::set<std::string>& reads) {
    if (!expr) return;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        reads.insert(ident->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectReads(binary->left.get(), reads);
        collectReads(binary->right.get(), reads);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectReads(unary->operand.get(), reads);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        collectReads(call->callee.get(), reads);
        for (auto& arg : call->args) {
            collectReads(arg.get(), reads);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        collectReads(index->object.get(), reads);
        collectReads(index->index.get(), reads);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        collectReads(member->object.get(), reads);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        collectReads(ternary->condition.get(), reads);
        collectReads(ternary->thenExpr.get(), reads);
        collectReads(ternary->elseExpr.get(), reads);
    }
}

void DeadStoreEliminationPass::collectReadsFromStmt(Statement* stmt, std::set<std::string>& reads) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectReads(exprStmt->expr.get(), reads);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            collectReads(varDecl->initializer.get(), reads);
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        collectReads(assignStmt->value.get(), reads);
        // For compound assignments, target is also read
        if (assignStmt->op != TokenType::ASSIGN) {
            if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                reads.insert(target->name);
            }
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            collectReads(returnStmt->value.get(), reads);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectReads(ifStmt->condition.get(), reads);
        collectReadsFromStmt(ifStmt->thenBranch.get(), reads);
        for (auto& elif : ifStmt->elifBranches) {
            collectReads(elif.first.get(), reads);
            collectReadsFromStmt(elif.second.get(), reads);
        }
        collectReadsFromStmt(ifStmt->elseBranch.get(), reads);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        collectReads(forStmt->iterable.get(), reads);
        collectReadsFromStmt(forStmt->body.get(), reads);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectReads(whileStmt->condition.get(), reads);
        collectReadsFromStmt(whileStmt->body.get(), reads);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectReadsFromStmt(s.get(), reads);
        }
    }
}

bool DeadStoreEliminationPass::isReadBetween(const std::vector<StmtPtr>& statements,
                                              const std::string& varName,
                                              size_t startIdx, size_t endIdx) {
    for (size_t i = startIdx + 1; i < endIdx && i < statements.size(); ++i) {
        std::set<std::string> reads;
        collectReadsFromStmt(statements[i].get(), reads);
        if (reads.count(varName)) {
            return true;
        }
    }
    return false;
}

bool DeadStoreEliminationPass::isReadAfter(const std::vector<StmtPtr>& statements,
                                            const std::string& varName,
                                            size_t startIdx) {
    for (size_t i = startIdx + 1; i < statements.size(); ++i) {
        std::set<std::string> reads;
        collectReadsFromStmt(statements[i].get(), reads);
        if (reads.count(varName)) {
            return true;
        }
    }
    return false;
}

bool DeadStoreEliminationPass::variableEscapes(const std::vector<StmtPtr>& statements,
                                                const std::string& varName,
                                                size_t startIdx) {
    // Check if variable is passed to a function call or returned
    for (size_t i = startIdx; i < statements.size(); ++i) {
        auto* stmt = statements[i].get();
        
        // Check for return statements
        if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
            if (returnStmt->value) {
                std::set<std::string> reads;
                collectReads(returnStmt->value.get(), reads);
                if (reads.count(varName)) {
                    return true;
                }
            }
        }
        
        // Check for function calls with the variable as argument
        std::function<bool(Expression*)> checkEscape = [&](Expression* expr) -> bool {
            if (!expr) return false;
            
            if (auto* call = dynamic_cast<CallExpr*>(expr)) {
                for (auto& arg : call->args) {
                    if (auto* ident = dynamic_cast<Identifier*>(arg.get())) {
                        if (ident->name == varName) {
                            return true;
                        }
                    }
                    if (checkEscape(arg.get())) return true;
                }
            }
            else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
                return checkEscape(binary->left.get()) || checkEscape(binary->right.get());
            }
            else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
                return checkEscape(unary->operand.get());
            }
            
            return false;
        };
        
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            if (checkEscape(exprStmt->expr.get())) {
                return true;
            }
        }
        else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            if (checkEscape(varDecl->initializer.get())) {
                return true;
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            if (checkEscape(assignStmt->value.get())) {
                return true;
            }
        }
    }
    
    return false;
}

void DeadStoreEliminationPass::removeDeadStores(std::vector<StmtPtr>& statements,
                                                 const std::vector<StoreInfo>& stores) {
    std::set<size_t> deadIndices;
    for (const auto& store : stores) {
        if (store.isDead) {
            deadIndices.insert(store.index);
        }
    }
    
    if (deadIndices.empty()) return;
    
    std::vector<StmtPtr> newStatements;
    newStatements.reserve(statements.size() - deadIndices.size());
    
    for (size_t i = 0; i < statements.size(); ++i) {
        if (deadIndices.find(i) == deadIndices.end()) {
            newStatements.push_back(std::move(statements[i]));
        }
    }
    
    statements = std::move(newStatements);
}

std::unique_ptr<DeadStoreEliminationPass> createDeadStoreEliminationPass() {
    return std::make_unique<DeadStoreEliminationPass>();
}

} // namespace tyl
