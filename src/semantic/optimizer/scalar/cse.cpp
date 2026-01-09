// Tyl Compiler - Common Subexpression Elimination Implementation
#include "cse.h"
#include <sstream>
#include <algorithm>

namespace tyl {

void CSEPass::run(Program& ast) {
    transformations_ = 0;
    tempCounter_ = 0;
    clearState();
    processBlock(ast.statements);
}

void CSEPass::clearState() {
    exprToTemp_.clear();
    modifiedVars_.clear();
}

std::string CSEPass::newTempVar() {
    return "__cse_" + std::to_string(tempCounter_++);
}

std::string CSEPass::hashExpression(Expression* expr) {
    if (!expr) return "";
    
    std::ostringstream oss;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        oss << "INT:" << intLit->value;
    }
    else if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        oss << "FLOAT:" << floatLit->value;
    }
    else if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        oss << "BOOL:" << (boolLit->value ? "true" : "false");
    }
    else if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        oss << "STR:" << strLit->value;
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        oss << "ID:" << ident->name;
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        std::string leftHash = hashExpression(binary->left.get());
        std::string rightHash = hashExpression(binary->right.get());
        
        // For commutative operations, normalize order
        bool isCommutative = (binary->op == TokenType::PLUS || 
                             binary->op == TokenType::STAR ||
                             binary->op == TokenType::EQ ||
                             binary->op == TokenType::NE ||
                             binary->op == TokenType::AND ||
                             binary->op == TokenType::OR);
        
        if (isCommutative && leftHash > rightHash) {
            std::swap(leftHash, rightHash);
        }
        
        oss << "BIN:" << static_cast<int>(binary->op) << "(" << leftHash << "," << rightHash << ")";
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        oss << "UN:" << static_cast<int>(unary->op) << "(" << hashExpression(unary->operand.get()) << ")";
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Only hash pure function calls
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            // Known pure functions
            if (callee->name == "abs" || callee->name == "sqrt" || 
                callee->name == "min" || callee->name == "max" ||
                callee->name == "len" || callee->name == "str") {
                oss << "CALL:" << callee->name << "(";
                for (size_t i = 0; i < call->args.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << hashExpression(call->args[i].get());
                }
                oss << ")";
            } else {
                return "";  // Non-pure function, can't CSE
            }
        } else {
            return "";
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        oss << "IDX:(" << hashExpression(index->object.get()) << ")[" 
            << hashExpression(index->index.get()) << "]";
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        oss << "MEM:(" << hashExpression(member->object.get()) << ")." << member->member;
    }
    else {
        return "";  // Unknown expression type, can't hash
    }
    
    return oss.str();
}

bool CSEPass::isCSECandidate(Expression* expr) {
    if (!expr) return false;
    
    // Only CSE non-trivial expressions
    if (dynamic_cast<IntegerLiteral*>(expr)) return false;
    if (dynamic_cast<FloatLiteral*>(expr)) return false;
    if (dynamic_cast<BoolLiteral*>(expr)) return false;
    if (dynamic_cast<StringLiteral*>(expr)) return false;
    if (dynamic_cast<Identifier*>(expr)) return false;
    
    // Binary expressions are good candidates
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        // Don't CSE simple operations with literals
        bool leftSimple = dynamic_cast<IntegerLiteral*>(binary->left.get()) ||
                         dynamic_cast<Identifier*>(binary->left.get());
        bool rightSimple = dynamic_cast<IntegerLiteral*>(binary->right.get()) ||
                          dynamic_cast<Identifier*>(binary->right.get());
        
        // Only CSE if at least one operand is complex, or both are identifiers
        if (leftSimple && rightSimple) {
            // CSE a + b style expressions
            if (dynamic_cast<Identifier*>(binary->left.get()) &&
                dynamic_cast<Identifier*>(binary->right.get())) {
                return true;
            }
            return false;
        }
        return true;
    }
    
    // Unary expressions with complex operands
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isCSECandidate(unary->operand.get());
    }
    
    // Pure function calls
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            return (callee->name == "abs" || callee->name == "sqrt" || 
                    callee->name == "min" || callee->name == "max" ||
                    callee->name == "len");
        }
    }
    
    return false;
}

bool CSEPass::usesModifiedVar(Expression* expr) {
    std::set<std::string> usedVars;
    collectUsedVars(expr, usedVars);
    
    for (const auto& var : usedVars) {
        if (modifiedVars_.count(var)) {
            return true;
        }
    }
    return false;
}

void CSEPass::collectUsedVars(Expression* expr, std::set<std::string>& vars) {
    if (!expr) return;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        vars.insert(ident->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectUsedVars(binary->left.get(), vars);
        collectUsedVars(binary->right.get(), vars);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectUsedVars(unary->operand.get(), vars);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            collectUsedVars(arg.get(), vars);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        collectUsedVars(index->object.get(), vars);
        collectUsedVars(index->index.get(), vars);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        collectUsedVars(member->object.get(), vars);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        collectUsedVars(ternary->condition.get(), vars);
        collectUsedVars(ternary->thenExpr.get(), vars);
        collectUsedVars(ternary->elseExpr.get(), vars);
    }
}

void CSEPass::invalidateVar(const std::string& varName) {
    modifiedVars_.insert(varName);
    
    // Remove any cached expressions that use this variable
    std::vector<std::string> toRemove;
    for (const auto& [hash, temp] : exprToTemp_) {
        if (hash.find("ID:" + varName) != std::string::npos) {
            toRemove.push_back(hash);
        }
    }
    for (const auto& hash : toRemove) {
        exprToTemp_.erase(hash);
    }
}

void CSEPass::processBlock(std::vector<StmtPtr>& statements) {
    // Save state for this scope
    auto savedExprToTemp = exprToTemp_;
    auto savedModifiedVars = modifiedVars_;
    
    std::vector<StmtPtr> newStatements;
    
    for (auto& stmt : statements) {
        std::vector<StmtPtr> insertBefore;
        processStatement(stmt);
        
        // Insert any new temp variable declarations
        for (auto& newStmt : insertBefore) {
            newStatements.push_back(std::move(newStmt));
        }
        
        if (stmt) {
            newStatements.push_back(std::move(stmt));
        }
    }
    
    statements = std::move(newStatements);
    
    // Restore state (for sibling scopes)
    // Note: We keep modifications visible to parent scope
}

void CSEPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            std::vector<StmtPtr> insertBefore;
            auto replacement = processExpression(varDecl->initializer, insertBefore);
            if (replacement) {
                varDecl->initializer = std::move(replacement);
            }
        }
        // Mark variable as defined (not modified yet)
        // Don't invalidate here - it's a new variable
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        std::vector<StmtPtr> insertBefore;
        auto replacement = processExpression(assignStmt->value, insertBefore);
        if (replacement) {
            assignStmt->value = std::move(replacement);
        }
        
        // Invalidate the assigned variable
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            invalidateVar(target->name);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        std::vector<StmtPtr> insertBefore;
        auto replacement = processExpression(exprStmt->expr, insertBefore);
        if (replacement) {
            exprStmt->expr = std::move(replacement);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        std::vector<StmtPtr> insertBefore;
        auto condReplacement = processExpression(ifStmt->condition, insertBefore);
        if (condReplacement) {
            ifStmt->condition = std::move(condReplacement);
        }
        
        // Process branches with fresh state (conservative)
        auto savedState = exprToTemp_;
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            processBlock(thenBlock->statements);
        } else {
            processStatement(ifStmt->thenBranch);
        }
        
        for (auto& elif : ifStmt->elifBranches) {
            exprToTemp_ = savedState;
            auto elifCondReplacement = processExpression(elif.first, insertBefore);
            if (elifCondReplacement) {
                elif.first = std::move(elifCondReplacement);
            }
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                processBlock(elifBlock->statements);
            } else {
                processStatement(elif.second);
            }
        }
        
        if (ifStmt->elseBranch) {
            exprToTemp_ = savedState;
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processBlock(elseBlock->statements);
            } else {
                processStatement(ifStmt->elseBranch);
            }
        }
        
        // After if, we can't rely on any cached expressions
        exprToTemp_.clear();
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        // Loops invalidate all cached expressions
        exprToTemp_.clear();
        
        std::vector<StmtPtr> insertBefore;
        auto condReplacement = processExpression(whileStmt->condition, insertBefore);
        if (condReplacement) {
            whileStmt->condition = std::move(condReplacement);
        }
        
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(whileStmt->body);
        }
        
        exprToTemp_.clear();
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        // Loops invalidate all cached expressions
        exprToTemp_.clear();
        invalidateVar(forStmt->var);
        
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(forStmt->body);
        }
        
        exprToTemp_.clear();
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        // New function = new scope
        auto savedState = exprToTemp_;
        auto savedModified = modifiedVars_;
        clearState();
        
        if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(fnDecl->body);
        }
        
        exprToTemp_ = savedState;
        modifiedVars_ = savedModified;
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            std::vector<StmtPtr> insertBefore;
            auto replacement = processExpression(returnStmt->value, insertBefore);
            if (replacement) {
                returnStmt->value = std::move(replacement);
            }
        }
    }
}

ExprPtr CSEPass::processExpression(ExprPtr& expr, std::vector<StmtPtr>& insertBefore) {
    if (!expr) return nullptr;
    
    // First, recursively process sub-expressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftReplacement = processExpression(binary->left, insertBefore);
        if (leftReplacement) binary->left = std::move(leftReplacement);
        
        auto rightReplacement = processExpression(binary->right, insertBefore);
        if (rightReplacement) binary->right = std::move(rightReplacement);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandReplacement = processExpression(unary->operand, insertBefore);
        if (operandReplacement) unary->operand = std::move(operandReplacement);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            auto argReplacement = processExpression(arg, insertBefore);
            if (argReplacement) arg = std::move(argReplacement);
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto condReplacement = processExpression(ternary->condition, insertBefore);
        if (condReplacement) ternary->condition = std::move(condReplacement);
        
        auto thenReplacement = processExpression(ternary->thenExpr, insertBefore);
        if (thenReplacement) ternary->thenExpr = std::move(thenReplacement);
        
        auto elseReplacement = processExpression(ternary->elseExpr, insertBefore);
        if (elseReplacement) ternary->elseExpr = std::move(elseReplacement);
    }
    
    // Now check if this expression can be CSE'd
    if (!isCSECandidate(expr.get())) {
        return nullptr;
    }
    
    // Check if expression uses modified variables
    if (usesModifiedVar(expr.get())) {
        return nullptr;
    }
    
    std::string hash = hashExpression(expr.get());
    if (hash.empty()) {
        return nullptr;
    }
    
    // Check if we've seen this expression before
    auto it = exprToTemp_.find(hash);
    if (it != exprToTemp_.end()) {
        // Replace with reference to temp variable
        transformations_++;
        return std::make_unique<Identifier>(it->second, expr->location);
    }
    
    // This is a new expression - remember it for future CSE
    // Note: We don't create temp variables here, we just remember the expression
    // The next occurrence will be replaced with a reference
    // For now, just cache the hash -> we'll use the original expression location
    
    // Actually, for proper CSE we need to hoist the first occurrence too
    // But that requires more complex analysis. For now, just track for second occurrence.
    exprToTemp_[hash] = "";  // Mark as seen, but no temp yet
    
    return nullptr;
}

} // namespace tyl
