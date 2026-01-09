// Tyl Compiler - Loop Idiom Recognition Implementation
#include "loop_idiom.h"
#include <algorithm>
#include <iostream>

namespace tyl {

void LoopIdiomRecognitionPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = LoopIdiomStats{};
    
    processStatements(ast.statements);
    
    transformations_ = stats_.memsetPatterns + stats_.memcpyPatterns;
}

void LoopIdiomRecognitionPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        // First recurse into nested structures
        processStatement(stmts[i]);
        
        // Try to recognize loop idioms in for loops
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmts[i].get())) {
            auto info = recognizeIdiom(forLoop);
            if (info.type != LoopIdiom::None) {
                auto transformed = transformIdiom(forLoop, info);
                if (transformed) {
                    stmts[i] = std::move(transformed);
                }
            }
        }
    }
}

void LoopIdiomRecognitionPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    // Recurse into nested structures
    if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
        if (fn->body) {
            if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        if (ifStmt->thenBranch) {
            if (auto* block = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                processStatements(block->statements);
            }
        }
        if (ifStmt->elseBranch) {
            if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processStatements(block->statements);
            }
        }
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.second) {
                if (auto* block = dynamic_cast<Block*>(elif.second.get())) {
                    processStatements(block->statements);
                }
            }
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        if (whileStmt->body) {
            if (auto* block = dynamic_cast<Block*>(whileStmt->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        if (forStmt->body) {
            if (auto* block = dynamic_cast<Block*>(forStmt->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processStatements(block->statements);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt.get())) {
        for (auto& c : matchStmt->cases) {
            if (c.body) {
                if (auto* block = dynamic_cast<Block*>(c.body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        if (matchStmt->defaultCase) {
            if (auto* block = dynamic_cast<Block*>(matchStmt->defaultCase.get())) {
                processStatements(block->statements);
            }
        }
    }
}

IdiomInfo LoopIdiomRecognitionPass::recognizeIdiom(ForStmt* loop) {
    IdiomInfo info;
    
    if (!loop || !loop->body || !loop->iterable) {
        ++stats_.loopsSkipped;
        return info;
    }
    
    // Don't transform labeled loops
    if (!loop->label.empty()) {
        ++stats_.loopsSkipped;
        return info;
    }
    
    // Check if it's a simple counting loop
    ExprPtr count;
    if (!isSimpleCountingLoop(loop, count)) {
        ++stats_.loopsSkipped;
        return info;
    }
    
    // Try memset pattern first
    if (isMemsetPattern(loop, info)) {
        info.count = std::move(count);
        return info;
    }
    
    // Try memcpy pattern
    if (isMemcpyPattern(loop, info)) {
        info.count = std::move(count);
        return info;
    }
    
    ++stats_.loopsSkipped;
    return info;
}

bool LoopIdiomRecognitionPass::isMemsetPattern(ForStmt* loop, IdiomInfo& info) {
    // Pattern: for i in 0..n { a[i] = value }
    // where value is loop-invariant
    
    // Get the single assignment in the body
    AssignStmt* assign = getSingleAssignment(loop->body.get());
    AssignExpr* assignExpr = nullptr;
    
    if (!assign) {
        assignExpr = getSingleAssignExpr(loop->body.get());
        if (!assignExpr) return false;
    }
    
    Expression* target = assign ? assign->target.get() : assignExpr->target.get();
    Expression* value = assign ? assign->value.get() : assignExpr->value.get();
    
    // Check if target is array[inductionVar]
    std::string arrayName;
    if (!isArrayIndexByInductionVar(target, loop->var, arrayName)) {
        return false;
    }
    
    // Check if value is loop-invariant
    if (!isLoopInvariant(value, loop->var)) {
        return false;
    }
    
    // Fill in the info
    info.type = LoopIdiom::Memset;
    info.destArray = arrayName;
    info.inductionVar = loop->var;
    info.fillValue = cloneExpression(value);
    info.startIndex = std::make_unique<IntegerLiteral>(0, loop->location);
    
    // Check if it's a constant fill value
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(value)) {
        info.isConstantFill = true;
        info.constantFillValue = intLit->value;
    }
    
    ++stats_.memsetPatterns;
    return true;
}

bool LoopIdiomRecognitionPass::isMemcpyPattern(ForStmt* loop, IdiomInfo& info) {
    // Pattern: for i in 0..n { a[i] = b[i] }
    
    // Get the single assignment in the body
    AssignStmt* assign = getSingleAssignment(loop->body.get());
    AssignExpr* assignExpr = nullptr;
    
    if (!assign) {
        assignExpr = getSingleAssignExpr(loop->body.get());
        if (!assignExpr) return false;
    }
    
    Expression* target = assign ? assign->target.get() : assignExpr->target.get();
    Expression* value = assign ? assign->value.get() : assignExpr->value.get();
    
    // Check if target is destArray[inductionVar]
    std::string destArray;
    if (!isArrayIndexByInductionVar(target, loop->var, destArray)) {
        return false;
    }
    
    // Check if value is srcArray[inductionVar]
    std::string srcArray;
    if (!isArrayIndexByInductionVar(value, loop->var, srcArray)) {
        return false;
    }
    
    // Make sure source and dest are different arrays
    if (destArray == srcArray) {
        return false;  // Self-copy, not a memcpy pattern
    }
    
    // Fill in the info
    info.type = LoopIdiom::Memcpy;
    info.destArray = destArray;
    info.srcArray = srcArray;
    info.inductionVar = loop->var;
    info.startIndex = std::make_unique<IntegerLiteral>(0, loop->location);
    
    ++stats_.memcpyPatterns;
    return true;
}

bool LoopIdiomRecognitionPass::isSimpleCountingLoop(ForStmt* loop, ExprPtr& count) {
    if (!loop->iterable) return false;
    
    // Check for RangeExpr: 0..n (exclusive)
    if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        // Check start is 0
        if (auto* startLit = dynamic_cast<IntegerLiteral*>(range->start.get())) {
            if (startLit->value != 0) return false;
        } else {
            return false;  // Non-constant start
        }
        
        // Check no step or step is 1
        if (range->step) {
            if (auto* stepLit = dynamic_cast<IntegerLiteral*>(range->step.get())) {
                if (stepLit->value != 1) return false;
            } else {
                return false;  // Non-constant step
            }
        }
        
        // Count is the end value (exclusive range)
        count = cloneExpression(range->end.get());
        return true;
    }
    
    // Check for InclusiveRangeExpr: 0..=n (inclusive)
    if (auto* range = dynamic_cast<InclusiveRangeExpr*>(loop->iterable.get())) {
        // Check start is 0
        if (auto* startLit = dynamic_cast<IntegerLiteral*>(range->start.get())) {
            if (startLit->value != 0) return false;
        } else {
            return false;
        }
        
        // Check no step or step is 1
        if (range->step) {
            if (auto* stepLit = dynamic_cast<IntegerLiteral*>(range->step.get())) {
                if (stepLit->value != 1) return false;
            } else {
                return false;
            }
        }
        
        // Count is end + 1 (inclusive range)
        auto endClone = cloneExpression(range->end.get());
        count = std::make_unique<BinaryExpr>(
            std::move(endClone),
            TokenType::PLUS,
            std::make_unique<IntegerLiteral>(1, range->location),
            range->location);
        return true;
    }
    
    // Check for range() function call
    if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == "range") {
                // range(n) - count is n
                if (call->args.size() == 1) {
                    count = cloneExpression(call->args[0].get());
                    return true;
                }
                // range(0, n) - count is n
                if (call->args.size() == 2) {
                    if (auto* startLit = dynamic_cast<IntegerLiteral*>(call->args[0].get())) {
                        if (startLit->value == 0) {
                            count = cloneExpression(call->args[1].get());
                            return true;
                        }
                    }
                }
                // range(0, n, 1) - count is n
                if (call->args.size() == 3) {
                    if (auto* startLit = dynamic_cast<IntegerLiteral*>(call->args[0].get())) {
                        if (startLit->value == 0) {
                            if (auto* stepLit = dynamic_cast<IntegerLiteral*>(call->args[2].get())) {
                                if (stepLit->value == 1) {
                                    count = cloneExpression(call->args[1].get());
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    return false;
}

bool LoopIdiomRecognitionPass::isArrayIndexByInductionVar(Expression* expr, 
                                                          const std::string& inductionVar,
                                                          std::string& arrayName) {
    auto* indexExpr = dynamic_cast<IndexExpr*>(expr);
    if (!indexExpr) return false;
    
    // Check if object is an identifier (array name)
    auto* arrayId = dynamic_cast<Identifier*>(indexExpr->object.get());
    if (!arrayId) return false;
    
    // Check if index is the induction variable
    auto* indexId = dynamic_cast<Identifier*>(indexExpr->index.get());
    if (!indexId || indexId->name != inductionVar) return false;
    
    arrayName = arrayId->name;
    return true;
}

bool LoopIdiomRecognitionPass::isLoopInvariant(Expression* expr, const std::string& inductionVar) {
    if (!expr) return true;
    
    // Literals are always invariant
    if (dynamic_cast<IntegerLiteral*>(expr) ||
        dynamic_cast<FloatLiteral*>(expr) ||
        dynamic_cast<BoolLiteral*>(expr) ||
        dynamic_cast<StringLiteral*>(expr) ||
        dynamic_cast<NilLiteral*>(expr)) {
        return true;
    }
    
    // Identifiers - check if it's the induction variable
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return id->name != inductionVar;
    }
    
    // Binary expressions - both sides must be invariant
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return isLoopInvariant(bin->left.get(), inductionVar) &&
               isLoopInvariant(bin->right.get(), inductionVar);
    }
    
    // Unary expressions
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return isLoopInvariant(un->operand.get(), inductionVar);
    }
    
    // Member access - object must be invariant
    if (auto* mem = dynamic_cast<MemberExpr*>(expr)) {
        return isLoopInvariant(mem->object.get(), inductionVar);
    }
    
    // Function calls are not invariant (may have side effects)
    if (dynamic_cast<CallExpr*>(expr)) {
        return false;
    }
    
    // Index expressions - both object and index must be invariant
    if (auto* idx = dynamic_cast<IndexExpr*>(expr)) {
        return isLoopInvariant(idx->object.get(), inductionVar) &&
               isLoopInvariant(idx->index.get(), inductionVar);
    }
    
    return false;
}

StmtPtr LoopIdiomRecognitionPass::transformIdiom(ForStmt* loop, const IdiomInfo& info) {
    switch (info.type) {
        case LoopIdiom::Memset:
            return createMemsetCall(info, loop->location);
        case LoopIdiom::Memcpy:
            return createMemcpyCall(info, loop->location);
        default:
            return nullptr;
    }
}

StmtPtr LoopIdiomRecognitionPass::createMemsetCall(const IdiomInfo& info, SourceLocation loc) {
    // Create: __builtin_memset(dest, value, count)
    // Or for Tyl: memset(dest, value, count)
    
    auto callee = std::make_unique<Identifier>("__builtin_memset", loc);
    auto call = std::make_unique<CallExpr>(std::move(callee), loc);
    
    // Argument 1: destination array
    call->args.push_back(std::make_unique<Identifier>(info.destArray, loc));
    
    // Argument 2: fill value
    if (info.fillValue) {
        call->args.push_back(cloneExpression(info.fillValue.get()));
    } else {
        call->args.push_back(std::make_unique<IntegerLiteral>(0, loc));
    }
    
    // Argument 3: count
    if (info.count) {
        call->args.push_back(cloneExpression(info.count.get()));
    }
    
    return std::make_unique<ExprStmt>(std::move(call), loc);
}

StmtPtr LoopIdiomRecognitionPass::createMemcpyCall(const IdiomInfo& info, SourceLocation loc) {
    // Create: __builtin_memcpy(dest, src, count)
    
    auto callee = std::make_unique<Identifier>("__builtin_memcpy", loc);
    auto call = std::make_unique<CallExpr>(std::move(callee), loc);
    
    // Argument 1: destination array
    call->args.push_back(std::make_unique<Identifier>(info.destArray, loc));
    
    // Argument 2: source array
    call->args.push_back(std::make_unique<Identifier>(info.srcArray, loc));
    
    // Argument 3: count
    if (info.count) {
        call->args.push_back(cloneExpression(info.count.get()));
    }
    
    return std::make_unique<ExprStmt>(std::move(call), loc);
}

AssignStmt* LoopIdiomRecognitionPass::getSingleAssignment(Statement* body) {
    if (!body) return nullptr;
    
    // Direct assignment statement
    if (auto* assign = dynamic_cast<AssignStmt*>(body)) {
        return assign;
    }
    
    // Block with single assignment
    if (auto* block = dynamic_cast<Block*>(body)) {
        if (block->statements.size() == 1) {
            return dynamic_cast<AssignStmt*>(block->statements[0].get());
        }
    }
    
    return nullptr;
}

AssignExpr* LoopIdiomRecognitionPass::getSingleAssignExpr(Statement* body) {
    if (!body) return nullptr;
    
    // Expression statement with assignment expression
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(body)) {
        return dynamic_cast<AssignExpr*>(exprStmt->expr.get());
    }
    
    // Block with single expression statement
    if (auto* block = dynamic_cast<Block*>(body)) {
        if (block->statements.size() == 1) {
            if (auto* exprStmt = dynamic_cast<ExprStmt*>(block->statements[0].get())) {
                return dynamic_cast<AssignExpr*>(exprStmt->expr.get());
            }
        }
    }
    
    return nullptr;
}

ExprPtr LoopIdiomRecognitionPass::cloneExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(lit->value, lit->location);
    }
    if (auto* lit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(lit->value, lit->location);
    }
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return std::make_unique<Identifier>(id->name, id->location);
    }
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpression(bin->left.get()),
            bin->op,
            cloneExpression(bin->right.get()),
            bin->location);
    }
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            un->op,
            cloneExpression(un->operand.get()),
            un->location);
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            cloneExpression(call->callee.get()),
            call->location);
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpression(arg.get()));
        }
        return newCall;
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpression(member->object.get()),
            member->member,
            member->location);
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpression(index->object.get()),
            cloneExpression(index->index.get()),
            index->location);
    }
    
    return nullptr;
}

ExprPtr LoopIdiomRecognitionPass::getRangeCount(Expression* range) {
    if (auto* rangeExpr = dynamic_cast<RangeExpr*>(range)) {
        // For exclusive range 0..n, count is n
        return cloneExpression(rangeExpr->end.get());
    }
    if (auto* inclRange = dynamic_cast<InclusiveRangeExpr*>(range)) {
        // For inclusive range 0..=n, count is n+1
        auto endClone = cloneExpression(inclRange->end.get());
        return std::make_unique<BinaryExpr>(
            std::move(endClone),
            TokenType::PLUS,
            std::make_unique<IntegerLiteral>(1, inclRange->location),
            inclRange->location);
    }
    return nullptr;
}

} // namespace tyl
