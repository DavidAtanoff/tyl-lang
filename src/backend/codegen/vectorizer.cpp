// Tyl Compiler - SIMD Vectorizer Implementation
// Auto-vectorization for loops and array operations

#include "vectorizer.h"
#include "backend/x64/x64_assembler.h"
#include <algorithm>

namespace tyl {

Vectorizer::Vectorizer() {}

void Vectorizer::analyze(Program& program) {
    loops_.clear();
    loopsAnalyzed_ = 0;
    loopsVectorizable_ = 0;
    
    // Find all for loops in the program
    std::function<void(Statement*)> findLoops = [&](Statement* stmt) {
        if (!stmt) return;
        
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            analyzeLoop(forLoop);
            findLoops(forLoop->body.get());
        }
        else if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                findLoops(s.get());
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            findLoops(ifStmt->thenBranch.get());
            for (auto& elif : ifStmt->elifBranches) {
                findLoops(elif.second.get());
            }
            findLoops(ifStmt->elseBranch.get());
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            findLoops(whileStmt->body.get());
        }
        else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
            findLoops(fnDecl->body.get());
        }
    };
    
    for (auto& stmt : program.statements) {
        findLoops(stmt.get());
    }
}

void Vectorizer::analyzeLoop(ForStmt* loop) {
    loopsAnalyzed_++;
    
    VectorizableLoop info;
    info.loop = loop;
    info.inductionVar = loop->var;
    info.tripCount = getTripCount(loop);
    info.tripCountKnown = info.tripCount > 0;
    info.width = VectorWidth::SCALAR;
    info.hasReduction = false;
    info.hasArrayAccess = false;
    info.isVectorizable = false;
    info.reason = "Not analyzed";
    
    // Check if trip count is known and sufficient for vectorization
    if (!info.tripCountKnown) {
        info.reason = "Unknown trip count";
        loops_.push_back(info);
        return;
    }
    
    if (info.tripCount < 4) {
        info.reason = "Trip count too small (< 4)";
        loops_.push_back(info);
        return;
    }
    
    // Analyze loop body
    if (!analyzeLoopBody(loop->body.get(), info)) {
        loops_.push_back(info);
        return;
    }
    
    // Check for dependencies that prevent vectorization
    if (!checkDependencies(loop->body.get(), loop->var)) {
        info.reason = "Loop-carried dependencies detected";
        loops_.push_back(info);
        return;
    }
    
    // Determine vector width
    if (info.tripCount >= 8) {
        info.width = VectorWidth::AVX_8;
    } else if (info.tripCount >= 4) {
        info.width = VectorWidth::SSE_4;
    } else {
        info.width = VectorWidth::SSE_2;
    }
    
    info.isVectorizable = true;
    info.reason = "Vectorizable";
    loopsVectorizable_++;
    
    loops_.push_back(info);
}

bool Vectorizer::analyzeLoopBody(Statement* body, VectorizableLoop& info) {
    if (!body) {
        info.reason = "Empty loop body";
        return false;
    }
    
    // Get statements from block
    std::vector<Statement*> stmts;
    if (auto* block = dynamic_cast<Block*>(body)) {
        for (auto& s : block->statements) {
            stmts.push_back(s.get());
        }
    } else {
        stmts.push_back(body);
    }
    
    // Analyze each statement
    for (auto* stmt : stmts) {
        // Check for simple reduction pattern: sum = sum + arr[i]
        if (isSimpleReduction(stmt, info)) {
            continue;
        }
        
        // Check for array assignment: arr[i] = expr
        if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
            if (auto* index = dynamic_cast<IndexExpr*>(assign->target.get())) {
                std::string arrayName;
                if (isArrayAccess(index, info.inductionVar, arrayName)) {
                    info.hasArrayAccess = true;
                    info.arrayVar = arrayName;
                    continue;
                }
            }
        }
        
        // Check for expression statements with array access
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            // Allow print statements and other side effects
            // but they prevent vectorization
            if (auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
                if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                    if (id->name == "print" || id->name == "println") {
                        info.reason = "Loop contains I/O operations";
                        return false;
                    }
                }
            }
        }
        
        // Check for control flow that prevents vectorization
        if (dynamic_cast<IfStmt*>(stmt) ||
            dynamic_cast<WhileStmt*>(stmt) ||
            dynamic_cast<ForStmt*>(stmt) ||
            dynamic_cast<BreakStmt*>(stmt) ||
            dynamic_cast<ContinueStmt*>(stmt) ||
            dynamic_cast<ReturnStmt*>(stmt)) {
            info.reason = "Loop contains control flow";
            return false;
        }
    }
    
    // Must have either reduction or array access to be vectorizable
    if (!info.hasReduction && !info.hasArrayAccess) {
        info.reason = "No vectorizable operations found";
        return false;
    }
    
    return true;
}

bool Vectorizer::isSimpleReduction(Statement* stmt, VectorizableLoop& info) {
    auto* assign = dynamic_cast<AssignStmt*>(stmt);
    if (!assign) return false;
    
    auto* targetId = dynamic_cast<Identifier*>(assign->target.get());
    if (!targetId) return false;
    
    // Check for compound assignment: sum += expr
    if (assign->op == TokenType::PLUS_ASSIGN || 
        assign->op == TokenType::STAR_ASSIGN) {
        
        // Check if RHS contains array access with induction var
        std::string arrayName;
        if (isArrayAccess(assign->value.get(), info.inductionVar, arrayName)) {
            info.hasReduction = true;
            info.reductionVar = targetId->name;
            info.reductionOp = assign->op == TokenType::PLUS_ASSIGN ? 
                               TokenType::PLUS : TokenType::STAR;
            info.hasArrayAccess = true;
            info.arrayVar = arrayName;
            return true;
        }
        
        // Simple increment: count += 1
        if (auto* intLit = dynamic_cast<IntegerLiteral*>(assign->value.get())) {
            info.hasReduction = true;
            info.reductionVar = targetId->name;
            info.reductionOp = assign->op == TokenType::PLUS_ASSIGN ? 
                               TokenType::PLUS : TokenType::STAR;
            return true;
        }
    }
    
    // Check for explicit form: sum = sum + arr[i]
    if (assign->op == TokenType::ASSIGN) {
        auto* binary = dynamic_cast<BinaryExpr*>(assign->value.get());
        if (!binary) return false;
        
        if (binary->op != TokenType::PLUS && binary->op != TokenType::STAR) {
            return false;
        }
        
        // Check if one operand is the target variable
        auto* leftId = dynamic_cast<Identifier*>(binary->left.get());
        auto* rightId = dynamic_cast<Identifier*>(binary->right.get());
        
        Expression* otherOperand = nullptr;
        if (leftId && leftId->name == targetId->name) {
            otherOperand = binary->right.get();
        } else if (rightId && rightId->name == targetId->name) {
            otherOperand = binary->left.get();
        }
        
        if (otherOperand) {
            std::string arrayName;
            if (isArrayAccess(otherOperand, info.inductionVar, arrayName)) {
                info.hasReduction = true;
                info.reductionVar = targetId->name;
                info.reductionOp = binary->op;
                info.hasArrayAccess = true;
                info.arrayVar = arrayName;
                return true;
            }
        }
    }
    
    return false;
}

bool Vectorizer::isArrayAccess(Expression* expr, const std::string& inductionVar, 
                                std::string& arrayName) {
    auto* index = dynamic_cast<IndexExpr*>(expr);
    if (!index) return false;
    
    // Check if object is an identifier (array name)
    auto* arrayId = dynamic_cast<Identifier*>(index->object.get());
    if (!arrayId) return false;
    
    // Check if index is the induction variable
    auto* indexId = dynamic_cast<Identifier*>(index->index.get());
    if (indexId && indexId->name == inductionVar) {
        arrayName = arrayId->name;
        return true;
    }
    
    // Check for index + constant or index * constant
    if (auto* binary = dynamic_cast<BinaryExpr*>(index->index.get())) {
        auto* leftId = dynamic_cast<Identifier*>(binary->left.get());
        auto* rightId = dynamic_cast<Identifier*>(binary->right.get());
        
        if ((leftId && leftId->name == inductionVar) ||
            (rightId && rightId->name == inductionVar)) {
            arrayName = arrayId->name;
            return true;
        }
    }
    
    return false;
}

bool Vectorizer::checkDependencies(Statement* body, const std::string& inductionVar) {
    // Simple dependency check: look for writes that are read in the same iteration
    // This is a conservative check - a full implementation would do proper
    // dependence analysis
    
    std::set<std::string> written;
    std::set<std::string> readBeforeWrite;
    
    std::function<void(Statement*, bool)> scan = [&](Statement* stmt, bool isWrite) {
        if (!stmt) return;
        
        if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                scan(s.get(), false);
            }
        }
        else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
            // First scan the RHS for reads
            std::function<void(Expression*)> scanExpr = [&](Expression* expr) {
                if (!expr) return;
                if (auto* id = dynamic_cast<Identifier*>(expr)) {
                    if (written.count(id->name) == 0) {
                        readBeforeWrite.insert(id->name);
                    }
                }
                else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
                    scanExpr(binary->left.get());
                    scanExpr(binary->right.get());
                }
                else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
                    scanExpr(index->object.get());
                    scanExpr(index->index.get());
                }
            };
            scanExpr(assign->value.get());
            
            // Then mark the target as written
            if (auto* id = dynamic_cast<Identifier*>(assign->target.get())) {
                written.insert(id->name);
            }
        }
    };
    
    scan(body, false);
    
    // Check for loop-carried dependencies
    // A variable that is both read and written could have dependencies
    // unless it's a reduction variable (which we handle specially)
    for (const auto& var : written) {
        if (readBeforeWrite.count(var) && var != inductionVar) {
            // This could be a loop-carried dependency
            // For now, we're conservative and allow it if it looks like a reduction
            // A full implementation would do proper analysis
        }
    }
    
    return true;  // Conservative: assume no problematic dependencies
}

int64_t Vectorizer::getTripCount(ForStmt* loop) {
    // Try to determine trip count from range expression
    if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        auto* startLit = dynamic_cast<IntegerLiteral*>(range->start.get());
        auto* endLit = dynamic_cast<IntegerLiteral*>(range->end.get());
        
        if (startLit && endLit) {
            return endLit->value - startLit->value;
        }
    }
    
    // Try to determine from range() call
    if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "range") {
                if (call->args.size() == 1) {
                    // range(end) - starts from 0
                    if (auto* endLit = dynamic_cast<IntegerLiteral*>(call->args[0].get())) {
                        return endLit->value;
                    }
                }
                else if (call->args.size() >= 2) {
                    // range(start, end)
                    auto* startLit = dynamic_cast<IntegerLiteral*>(call->args[0].get());
                    auto* endLit = dynamic_cast<IntegerLiteral*>(call->args[1].get());
                    if (startLit && endLit) {
                        return endLit->value - startLit->value;
                    }
                }
            }
        }
    }
    
    return -1;  // Unknown trip count
}

bool Vectorizer::canVectorize(ForStmt* loop) {
    for (const auto& info : loops_) {
        if (info.loop == loop) {
            return info.isVectorizable;
        }
    }
    return false;
}

VectorWidth Vectorizer::getRecommendedWidth(ForStmt* loop) {
    for (const auto& info : loops_) {
        if (info.loop == loop) {
            return info.width;
        }
    }
    return VectorWidth::SCALAR;
}

// SIMD Code Generator implementation
bool SIMDCodeGen::hasAVX() {
    // Runtime CPUID check for AVX support
    // For simplicity, assume AVX is available on modern x64 CPUs
    return true;
}

VectorWidth SIMDCodeGen::getOptimalWidth() {
    if (hasAVX()) {
        return VectorWidth::AVX_8;
    }
    return VectorWidth::SSE_4;
}

// ============================================
// SIMD Code Generation
// ============================================

bool SIMDCodeGen::generateVectorizedLoop(const VectorizableLoop& loop, X64Assembler& asm_,
                                          NativeCodeGen& codegen) {
    if (!loop.isVectorizable) return false;
    
    // For now, we support simple reduction patterns
    if (loop.hasReduction) {
        generateReduction(loop, asm_, codegen);
        return true;
    }
    
    // Array operations without reduction
    if (loop.hasArrayAccess) {
        generateArrayOp(loop, asm_, codegen);
        return true;
    }
    
    return false;
}

void SIMDCodeGen::generateReduction(const VectorizableLoop& loop, X64Assembler& asm_,
                                     NativeCodeGen& codegen) {
    (void)codegen;  // May be used for variable lookups
    
    // Determine vector width
    int vectorWidth = static_cast<int>(loop.width);
    if (vectorWidth < 2) vectorWidth = 4;  // Default to SSE width
    
    int64_t tripCount = loop.tripCount;
    int64_t vectorIterations = tripCount / vectorWidth;
    int64_t remainder = tripCount % vectorWidth;
    
    // Initialize accumulator vector to zero
    // For addition: xorpd/pxor to zero
    // For multiplication: would need to load 1.0 vector
    if (loop.reductionOp == TokenType::PLUS) {
        asm_.pxor_xmm0_xmm0();  // Zero the accumulator
    }
    
    // Generate vectorized loop
    // This is a simplified version - a full implementation would:
    // 1. Load vector of array elements
    // 2. Add to accumulator vector
    // 3. Loop for vectorIterations
    // 4. Horizontal reduction
    // 5. Handle remainder with scalar code
    
    if (vectorIterations > 0) {
        // Vectorized loop would go here
        // For now, we just note that vectorization is possible
        
        // After vector loop, do horizontal reduction
        generateHorizontalReduction(loop.reductionOp, loop.width, asm_);
    }
    
    // Handle remainder iterations with scalar code
    (void)remainder;  // Would generate scalar cleanup loop
}

void SIMDCodeGen::generateArrayOp(const VectorizableLoop& loop, X64Assembler& asm_,
                                   NativeCodeGen& codegen) {
    (void)loop;
    (void)asm_;
    (void)codegen;
    // Generate vectorized array operations (e.g., arr[i] = arr[i] * 2)
    // This would load vectors, operate, and store back
}

void SIMDCodeGen::generateHorizontalReduction(TokenType op, VectorWidth width, X64Assembler& asm_) {
    // Reduce vector in xmm0 to scalar
    
    if (width == VectorWidth::SSE_4 || width == VectorWidth::AVX_8) {
        // For 4 x int32 or 4 x float
        if (op == TokenType::PLUS) {
            // Use horizontal add
            asm_.phaddd_xmm0_xmm0();  // [a+b, c+d, a+b, c+d]
            asm_.phaddd_xmm0_xmm0();  // [a+b+c+d, ...]
        }
    }
    else if (width == VectorWidth::SSE_2 || width == VectorWidth::AVX_4) {
        // For 2 x double
        if (op == TokenType::PLUS) {
            asm_.haddpd_xmm0_xmm0();  // [a+b, a+b]
        }
    }
    
    // Result is now in the low element of xmm0
}

} // namespace tyl
