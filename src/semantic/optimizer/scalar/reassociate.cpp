// Tyl Compiler - Reassociate Pass Implementation
// Reorders commutative/associative operations to expose more constant folding and CSE opportunities
#include "reassociate.h"
#include <algorithm>

namespace tyl {

void ReassociatePass::run(Program& ast) {
    transformations_ = 0;
    processStatements(ast.statements);
}

void ReassociatePass::processStatements(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        processStatement(stmt);
    }
}

void ReassociatePass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        // Reset ranks for each function
        varRanks_.clear();
        nextRank_ = 0;
        
        // Assign ranks to parameters first (they have lowest rank)
        for (const auto& param : fnDecl->params) {
            varRanks_[param.first] = nextRank_++;
        }
        
        // Assign ranks to variables in function body
        if (fnDecl->body) {
            assignRanks(fnDecl->body.get());
        }
        
        // Process function body
        if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
            processStatements(body->statements);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processStatements(block->statements);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            varDecl->initializer = processExpression(varDecl->initializer);
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        assignStmt->value = processExpression(assignStmt->value);
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        exprStmt->expr = processExpression(exprStmt->expr);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            returnStmt->value = processExpression(returnStmt->value);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        ifStmt->condition = processExpression(ifStmt->condition);
        processStatement(ifStmt->thenBranch);
        for (auto& elif : ifStmt->elifBranches) {
            elif.first = processExpression(elif.first);
            processStatement(elif.second);
        }
        if (ifStmt->elseBranch) {
            processStatement(ifStmt->elseBranch);
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        forStmt->iterable = processExpression(forStmt->iterable);
        processStatement(forStmt->body);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        whileStmt->condition = processExpression(whileStmt->condition);
        processStatement(whileStmt->body);
    }
}

void ReassociatePass::assignRanks(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varRanks_.find(varDecl->name) == varRanks_.end()) {
            varRanks_[varDecl->name] = nextRank_++;
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            assignRanks(s.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        assignRanks(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            assignRanks(elif.second.get());
        }
        assignRanks(ifStmt->elseBranch.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        // Loop variable
        if (varRanks_.find(forStmt->var) == varRanks_.end()) {
            varRanks_[forStmt->var] = nextRank_++;
        }
        assignRanks(forStmt->body.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        assignRanks(whileStmt->body.get());
    }
}

ExprPtr ReassociatePass::processExpression(ExprPtr& expr) {
    if (!expr) return std::move(expr);
    
    // First, recursively process sub-expressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        binary->left = processExpression(binary->left);
        binary->right = processExpression(binary->right);
        
        // Check if this is a reassociable operation
        if (isReassociable(binary->op)) {
            // Linearize the expression tree
            std::vector<RankedOperand> operands;
            linearize(expr.get(), binary->op, operands);
            
            // Only reassociate if we have multiple operands
            if (operands.size() >= 2) {
                // Try to fold constants
                bool folded = foldConstants(operands, binary->op);
                
                // Sort operands by rank (constants last)
                std::sort(operands.begin(), operands.end());
                
                // Rebuild the tree
                ExprPtr newExpr = rebuildTree(operands, binary->op, binary->location);
                
                if (folded || operands.size() >= 2) {
                    transformations_++;
                    return newExpr;
                }
            }
        }
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        unary->operand = processExpression(unary->operand);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        call->callee = processExpression(call->callee);
        for (auto& arg : call->args) {
            arg = processExpression(arg);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        index->object = processExpression(index->object);
        index->index = processExpression(index->index);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr.get())) {
        member->object = processExpression(member->object);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        ternary->condition = processExpression(ternary->condition);
        ternary->thenExpr = processExpression(ternary->thenExpr);
        ternary->elseExpr = processExpression(ternary->elseExpr);
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr.get())) {
        assign->value = processExpression(assign->value);
    }
    
    return std::move(expr);
}

bool ReassociatePass::isReassociable(TokenType op) {
    // Commutative and associative operations
    switch (op) {
        case TokenType::PLUS:
        case TokenType::STAR:
        case TokenType::AMP:        // Bitwise AND
        case TokenType::PIPE:       // Bitwise OR
        case TokenType::CARET:      // Bitwise XOR
        case TokenType::AND:        // Logical AND
        case TokenType::OR:         // Logical OR
            return true;
        default:
            return false;
    }
}

void ReassociatePass::linearize(Expression* expr, TokenType op, std::vector<RankedOperand>& operands) {
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        if (binary->op == op) {
            // Same operation - recurse into both sides
            linearize(binary->left.get(), op, operands);
            linearize(binary->right.get(), op, operands);
            return;
        }
    }
    
    // Not the same operation or not a binary expr - this is a leaf operand
    RankedOperand ranked;
    ranked.expr = cloneExpr(expr);
    ranked.rank = computeRank(expr);
    ranked.isConstant = isConstant(expr, ranked.constValue);
    operands.push_back(std::move(ranked));
}

int ReassociatePass::computeRank(Expression* expr) {
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = varRanks_.find(ident->name);
        if (it != varRanks_.end()) {
            return it->second;
        }
        // Unknown variable gets high rank
        return 1000;
    }
    
    if (isConstant(expr)) {
        // Constants get highest rank (go last)
        return 10000;
    }
    
    // Complex expressions get medium-high rank
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::max(computeRank(binary->left.get()), computeRank(binary->right.get())) + 1;
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        return 500;  // Function calls have high rank
    }
    
    return 100;  // Default rank for other expressions
}

bool ReassociatePass::foldConstants(std::vector<RankedOperand>& operands, TokenType op) {
    // Find all constants
    std::vector<size_t> constIndices;
    for (size_t i = 0; i < operands.size(); ++i) {
        if (operands[i].isConstant) {
            constIndices.push_back(i);
        }
    }
    
    // Need at least 2 constants to fold
    if (constIndices.size() < 2) {
        return false;
    }
    
    // Fold all constants into one
    int64_t result = operands[constIndices[0]].constValue;
    SourceLocation loc = operands[constIndices[0]].expr->location;
    
    for (size_t i = 1; i < constIndices.size(); ++i) {
        result = evalConstant(op, result, operands[constIndices[i]].constValue);
    }
    
    // Remove all constants except the first, update the first with folded value
    std::vector<RankedOperand> newOperands;
    bool addedFolded = false;
    
    for (size_t i = 0; i < operands.size(); ++i) {
        if (!operands[i].isConstant) {
            newOperands.push_back(std::move(operands[i]));
        } else if (!addedFolded) {
            // Add the folded constant
            RankedOperand folded;
            folded.expr = std::make_unique<IntegerLiteral>(result, loc);
            folded.rank = 10000;
            folded.isConstant = true;
            folded.constValue = result;
            newOperands.push_back(std::move(folded));
            addedFolded = true;
        }
        // Skip other constants
    }
    
    operands = std::move(newOperands);
    return true;
}

ExprPtr ReassociatePass::rebuildTree(std::vector<RankedOperand>& operands, TokenType op, const SourceLocation& loc) {
    if (operands.empty()) {
        // Return identity element
        switch (op) {
            case TokenType::PLUS:
                return std::make_unique<IntegerLiteral>(0, loc);
            case TokenType::STAR:
                return std::make_unique<IntegerLiteral>(1, loc);
            case TokenType::AND:
                return std::make_unique<BoolLiteral>(true, loc);
            case TokenType::OR:
                return std::make_unique<BoolLiteral>(false, loc);
            default:
                return std::make_unique<IntegerLiteral>(0, loc);
        }
    }
    
    if (operands.size() == 1) {
        return std::move(operands[0].expr);
    }
    
    // Build a left-associative tree
    // This puts constants at the end where they can be folded
    ExprPtr result = std::move(operands[0].expr);
    
    for (size_t i = 1; i < operands.size(); ++i) {
        result = std::make_unique<BinaryExpr>(
            std::move(result),
            op,
            std::move(operands[i].expr),
            loc
        );
    }
    
    return result;
}

ExprPtr ReassociatePass::cloneExpr(Expression* expr) {
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
    
    // For other expression types, return nullptr (shouldn't happen in practice)
    return nullptr;
}

bool ReassociatePass::isConstant(Expression* expr) {
    int64_t dummy;
    return isConstant(expr, dummy);
}

bool ReassociatePass::isConstant(Expression* expr, int64_t& value) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        value = intLit->value;
        return true;
    }
    return false;
}

int64_t ReassociatePass::evalConstant(TokenType op, int64_t left, int64_t right) {
    switch (op) {
        case TokenType::PLUS:
            return left + right;
        case TokenType::STAR:
            return left * right;
        case TokenType::AMP:
            return left & right;
        case TokenType::PIPE:
            return left | right;
        case TokenType::CARET:
            return left ^ right;
        default:
            return left;
    }
}

std::unique_ptr<ReassociatePass> createReassociatePass() {
    return std::make_unique<ReassociatePass>();
}

} // namespace tyl
