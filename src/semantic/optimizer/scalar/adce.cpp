// Tyl Compiler - Aggressive Dead Code Elimination Implementation
// Uses reverse dataflow analysis for liveness-based DCE
#include "adce.h"
#include "dead_code.h"
#include <algorithm>

namespace tyl {

// ============================================
// ADCE Pass Implementation
// ============================================

void ADCEPass::run(Program& ast) {
    transformations_ = 0;
    livenessInfo_.clear();
    liveStatements_.clear();
    
    // Process each function
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    processFunction(fn);
                }
            }
        }
    }
}

void ADCEPass::processFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (!body) return;
    
    livenessInfo_.clear();
    liveStatements_.clear();
    
    // Phase 1: Compute def/use for all statements (including nested)
    computeDefUseRecursive(body->statements);
    
    // Phase 2: Mark initially live statements
    markInitiallyLive(body->statements);
    
    // Phase 3: Propagate liveness
    propagateLiveness(body->statements);
    
    // Phase 4: Remove dead statements
    removeDeadStatements(body->statements);
}

void ADCEPass::computeDefUseRecursive(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        LivenessInfo info;
        computeDefUse(stmt.get(), info);
        livenessInfo_[stmt.get()] = info;
        
        // Recurse into nested blocks
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                computeDefUseRecursive(thenBlock->statements);
            } else if (ifStmt->thenBranch) {
                // Single statement, not a block
                std::vector<StmtPtr> temp;
                // Can't move, just process in place
                LivenessInfo thenInfo;
                computeDefUse(ifStmt->thenBranch.get(), thenInfo);
                livenessInfo_[ifStmt->thenBranch.get()] = thenInfo;
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    computeDefUseRecursive(elifBlock->statements);
                } else if (elif.second) {
                    LivenessInfo elifInfo;
                    computeDefUse(elif.second.get(), elifInfo);
                    livenessInfo_[elif.second.get()] = elifInfo;
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                computeDefUseRecursive(elseBlock->statements);
            } else if (ifStmt->elseBranch) {
                LivenessInfo elseInfo;
                computeDefUse(ifStmt->elseBranch.get(), elseInfo);
                livenessInfo_[ifStmt->elseBranch.get()] = elseInfo;
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                computeDefUseRecursive(body->statements);
            } else if (whileStmt->body) {
                LivenessInfo bodyInfo;
                computeDefUse(whileStmt->body.get(), bodyInfo);
                livenessInfo_[whileStmt->body.get()] = bodyInfo;
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                computeDefUseRecursive(body->statements);
            } else if (forStmt->body) {
                LivenessInfo bodyInfo;
                computeDefUse(forStmt->body.get(), bodyInfo);
                livenessInfo_[forStmt->body.get()] = bodyInfo;
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            computeDefUseRecursive(block->statements);
        }
    }
}

void ADCEPass::computeDefUse(Statement* stmt, LivenessInfo& info) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        info.def.insert(varDecl->name);
        if (varDecl->initializer) {
            computeDefUseExpr(varDecl->initializer.get(), info.use);
            // Mark as having side effects if initializer has function call
            if (containsFunctionCall(varDecl->initializer.get())) {
                info.hasSideEffects = true;
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            // For compound assignments, target is also used
            if (assignStmt->op != TokenType::ASSIGN) {
                info.use.insert(target->name);
            }
            info.def.insert(target->name);
        } else {
            // Complex target (array index, member access) - treat as use and side effect
            computeDefUseExpr(assignStmt->target.get(), info.use);
            info.hasSideEffects = true;  // Array/member assignment is a side effect
        }
        computeDefUseExpr(assignStmt->value.get(), info.use);
        // Mark as having side effects if value has function call
        if (containsFunctionCall(assignStmt->value.get())) {
            info.hasSideEffects = true;
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        computeDefUseExpr(exprStmt->expr.get(), info.use);
        
        // Expression statements with function calls have side effects
        if (containsFunctionCall(exprStmt->expr.get())) {
            info.hasSideEffects = true;
        }
        
        // Handle AssignExpr inside ExprStmt
        if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* target = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                if (assignExpr->op != TokenType::ASSIGN) {
                    info.use.insert(target->name);
                }
                info.def.insert(target->name);
            } else {
                // Complex target - side effect
                info.hasSideEffects = true;
            }
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            computeDefUseExpr(returnStmt->value.get(), info.use);
        }
        info.hasSideEffects = true;  // Returns are always live
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        computeDefUseExpr(ifStmt->condition.get(), info.use);
        info.hasSideEffects = true;  // Control flow is live
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        computeDefUseExpr(whileStmt->condition.get(), info.use);
        info.hasSideEffects = true;
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        computeDefUseExpr(forStmt->iterable.get(), info.use);
        info.def.insert(forStmt->var);
        info.hasSideEffects = true;
    }
    else if (dynamic_cast<BreakStmt*>(stmt) || dynamic_cast<ContinueStmt*>(stmt)) {
        info.hasSideEffects = true;
    }
    else if (dynamic_cast<Block*>(stmt)) {
        // Blocks themselves don't have side effects, but their contents might
        // The contents are handled by computeDefUseRecursive
    }
}

void ADCEPass::computeDefUseExpr(Expression* expr, std::set<std::string>& uses) {
    if (!expr) return;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        uses.insert(ident->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        computeDefUseExpr(binary->left.get(), uses);
        computeDefUseExpr(binary->right.get(), uses);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        computeDefUseExpr(unary->operand.get(), uses);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        computeDefUseExpr(call->callee.get(), uses);
        for (auto& arg : call->args) {
            computeDefUseExpr(arg.get(), uses);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        computeDefUseExpr(index->object.get(), uses);
        computeDefUseExpr(index->index.get(), uses);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        computeDefUseExpr(member->object.get(), uses);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        computeDefUseExpr(ternary->condition.get(), uses);
        computeDefUseExpr(ternary->thenExpr.get(), uses);
        computeDefUseExpr(ternary->elseExpr.get(), uses);
    }
    else if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        computeDefUseExpr(assignExpr->target.get(), uses);
        computeDefUseExpr(assignExpr->value.get(), uses);
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            computeDefUseExpr(elem.get(), uses);
        }
    }
    else if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        for (auto& field : record->fields) {
            computeDefUseExpr(field.second.get(), uses);
        }
    }
    else if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        computeDefUseExpr(range->start.get(), uses);
        computeDefUseExpr(range->end.get(), uses);
        if (range->step) computeDefUseExpr(range->step.get(), uses);
    }
    else if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        for (auto& part : interp->parts) {
            if (std::holds_alternative<ExprPtr>(part)) {
                computeDefUseExpr(std::get<ExprPtr>(part).get(), uses);
            }
        }
    }
}

void ADCEPass::markInitiallyLive(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        auto& info = livenessInfo_[stmt.get()];
        
        // Statements with side effects are initially live
        if (info.hasSideEffects || hasSideEffects(stmt.get())) {
            markLive(stmt.get());
        }
        
        // Process nested blocks
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            // Mark the if statement itself as live (it has control flow)
            markLive(stmt.get());
            
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                markInitiallyLive(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    markInitiallyLive(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                markInitiallyLive(elseBlock->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            // Mark the while statement itself as live
            markLive(stmt.get());
            
            // Mark all statements in loop body as live (conservative)
            // Loop bodies are complex - modifications may be read in next iteration
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                for (auto& bodyStmt : body->statements) {
                    markLive(bodyStmt.get());
                }
                markInitiallyLive(body->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            // Mark the for statement itself as live
            markLive(stmt.get());
            
            // Mark all statements in loop body as live (conservative)
            // Loop bodies are complex - modifications may be read in next iteration
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                for (auto& bodyStmt : body->statements) {
                    markLive(bodyStmt.get());
                }
                markInitiallyLive(body->statements);
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            markInitiallyLive(block->statements);
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            // Expression statements with function calls are always live
            if (containsFunctionCall(exprStmt->expr.get())) {
                markLive(stmt.get());
            }
        }
    }
}

bool ADCEPass::hasSideEffects(Statement* stmt) {
    if (!stmt) return false;
    
    if (dynamic_cast<ReturnStmt*>(stmt)) return true;
    if (dynamic_cast<BreakStmt*>(stmt)) return true;
    if (dynamic_cast<ContinueStmt*>(stmt)) return true;
    if (dynamic_cast<IfStmt*>(stmt)) return true;
    if (dynamic_cast<WhileStmt*>(stmt)) return true;
    if (dynamic_cast<ForStmt*>(stmt)) return true;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        // Any expression statement with a function call has side effects
        // This is conservative but safe
        if (containsFunctionCall(exprStmt->expr.get())) {
            return true;
        }
        return exprHasSideEffects(exprStmt->expr.get());
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            // Variable declarations with function calls have side effects
            if (containsFunctionCall(varDecl->initializer.get())) {
                return true;
            }
            return exprHasSideEffects(varDecl->initializer.get());
        }
        return false;
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        // Assignments to non-local variables have side effects
        if (!dynamic_cast<Identifier*>(assignStmt->target.get())) {
            return true;  // Array/member assignment
        }
        // Assignments with function calls have side effects
        if (containsFunctionCall(assignStmt->value.get())) {
            return true;
        }
        return exprHasSideEffects(assignStmt->value.get());
    }
    
    return false;
}

bool ADCEPass::containsFunctionCall(Expression* expr) {
    if (!expr) return false;
    
    if (dynamic_cast<CallExpr*>(expr)) {
        return true;
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return containsFunctionCall(binary->left.get()) || 
               containsFunctionCall(binary->right.get());
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return containsFunctionCall(unary->operand.get());
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return containsFunctionCall(ternary->condition.get()) ||
               containsFunctionCall(ternary->thenExpr.get()) ||
               containsFunctionCall(ternary->elseExpr.get());
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return containsFunctionCall(index->object.get()) ||
               containsFunctionCall(index->index.get());
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return containsFunctionCall(member->object.get());
    }
    
    if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            if (containsFunctionCall(elem.get())) return true;
        }
    }
    
    return false;
}

bool ADCEPass::exprHasSideEffects(Expression* expr) {
    if (!expr) return false;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Most function calls have side effects
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            // Known pure functions
            static const std::set<std::string> pureFunctions = {
                "abs", "sqrt", "sin", "cos", "tan", "min", "max",
                "floor", "ceil", "round", "len", "str"
            };
            if (pureFunctions.count(callee->name)) {
                // Check if arguments have side effects
                for (auto& arg : call->args) {
                    if (exprHasSideEffects(arg.get())) return true;
                }
                return false;
            }
        }
        return true;  // Unknown function - assume side effects
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return exprHasSideEffects(binary->left.get()) || 
               exprHasSideEffects(binary->right.get());
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return exprHasSideEffects(unary->operand.get());
    }
    
    if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        return true;  // Assignments have side effects
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return exprHasSideEffects(ternary->condition.get()) ||
               exprHasSideEffects(ternary->thenExpr.get()) ||
               exprHasSideEffects(ternary->elseExpr.get());
    }
    
    return false;
}

void ADCEPass::markLive(Statement* stmt) {
    if (!stmt) return;
    if (liveStatements_.count(stmt)) return;  // Already marked
    
    liveStatements_.insert(stmt);
    livenessInfo_[stmt].isLive = true;
    worklist_.push(stmt);
}

void ADCEPass::propagateLiveness(std::vector<StmtPtr>& stmts) {
    // Process worklist - propagate liveness to definitions of used variables
    while (!worklist_.empty()) {
        Statement* stmt = worklist_.front();
        worklist_.pop();
        
        auto& info = livenessInfo_[stmt];
        
        // For each variable used by this statement, mark its definition as live
        for (const auto& var : info.use) {
            std::vector<Statement*> defs;
            findDefiningStatements(var, stmts, defs);
            for (auto* def : defs) {
                if (!liveStatements_.count(def)) {
                    markLive(def);
                }
            }
        }
    }
}

void ADCEPass::findDefiningStatements(const std::string& var,
                                       std::vector<StmtPtr>& stmts,
                                       std::vector<Statement*>& result) {
    for (auto& stmt : stmts) {
        auto it = livenessInfo_.find(stmt.get());
        if (it != livenessInfo_.end() && it->second.def.count(var)) {
            result.push_back(stmt.get());
        }
        
        // Search nested blocks
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                findDefiningStatements(var, thenBlock->statements, result);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    findDefiningStatements(var, elifBlock->statements, result);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                findDefiningStatements(var, elseBlock->statements, result);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                findDefiningStatements(var, body->statements, result);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (var == forStmt->var) {
                result.push_back(stmt.get());
            }
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                findDefiningStatements(var, body->statements, result);
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            findDefiningStatements(var, block->statements, result);
        }
    }
}

void ADCEPass::removeDeadStatements(std::vector<StmtPtr>& stmts) {
    auto it = stmts.begin();
    while (it != stmts.end()) {
        Statement* stmt = it->get();
        
        // Process nested blocks first
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                removeDeadStatements(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    removeDeadStatements(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                removeDeadStatements(elseBlock->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                removeDeadStatements(body->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                removeDeadStatements(body->statements);
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt)) {
            removeDeadStatements(block->statements);
        }
        
        // Check if this statement is dead
        auto infoIt = livenessInfo_.find(stmt);
        bool hasInfo = (infoIt != livenessInfo_.end());
        bool isLive = hasInfo && infoIt->second.isLive;
        bool hasSideEffectsFlag = hasInfo && infoIt->second.hasSideEffects;
        
        // Only remove if we have liveness info and it's not live and has no side effects
        if (hasInfo && !isLive && !hasSideEffectsFlag) {
            // Don't remove control flow statements
            if (!dynamic_cast<IfStmt*>(stmt) && 
                !dynamic_cast<WhileStmt*>(stmt) && 
                !dynamic_cast<ForStmt*>(stmt) &&
                !dynamic_cast<Block*>(stmt)) {
                it = stmts.erase(it);
                transformations_++;
                continue;
            }
        }
        
        ++it;
    }
}

// ============================================
// Enhanced DCE Pass Implementation
// ============================================

void EnhancedDCEPass::run(Program& ast) {
    transformations_ = 0;
    
    // Run traditional DCE first
    runTraditionalDCE(ast);
    
    // Then run ADCE for more aggressive elimination
    runADCE(ast);
}

void EnhancedDCEPass::runTraditionalDCE(Program& ast) {
    DeadCodeEliminationPass dce;
    dce.run(ast);
    transformations_ += dce.transformations();
}

void EnhancedDCEPass::runADCE(Program& ast) {
    ADCEPass adce;
    adce.run(ast);
    transformations_ += adce.transformations();
}

// Factory function
std::unique_ptr<ADCEPass> createADCEPass() {
    return std::make_unique<ADCEPass>();
}

std::unique_ptr<EnhancedDCEPass> createEnhancedDCEPass() {
    return std::make_unique<EnhancedDCEPass>();
}

} // namespace tyl
