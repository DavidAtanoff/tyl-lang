// Tyl Compiler - Bit-Tracking Dead Code Elimination (BDCE)
// Removes code that computes bits that are never used
#ifndef TYL_BDCE_H
#define TYL_BDCE_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>
#include <cstdint>

namespace tyl {

// Represents which bits of a value are actually demanded/used
struct DemandedBits {
    uint64_t mask = ~0ULL;  // All bits demanded by default
    int bitWidth = 64;      // Bit width of the value
    
    DemandedBits() = default;
    DemandedBits(uint64_t m, int w) : mask(m), bitWidth(w) {}
    
    bool isAllOnes() const { 
        return mask == ((1ULL << bitWidth) - 1) || mask == ~0ULL; 
    }
    bool isZero() const { return mask == 0; }
    
    // Check if specific bits are demanded
    bool isDemanded(int bit) const { return (mask >> bit) & 1; }
    
    // Get number of leading zeros (unused high bits)
    int countLeadingZeros() const;
    
    // Intersect with another mask
    DemandedBits intersect(const DemandedBits& other) const {
        return DemandedBits(mask & other.mask, std::min(bitWidth, other.bitWidth));
    }
};

// Information about demanded bits for each expression
struct BitTrackingInfo {
    DemandedBits demanded;      // Which bits are demanded by users
    bool isDead = false;        // Is this expression dead?
    bool canSimplify = false;   // Can this be simplified?
};

// Bit-Tracking Dead Code Elimination Pass
// Tracks which bits of each value are actually used and removes/simplifies
// code that computes unused bits
class BDCEPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "BDCE"; }
    
private:
    // Bit tracking info for each expression (indexed by pointer)
    std::map<Expression*, BitTrackingInfo> bitInfo_;
    
    // Set of expressions that are dead
    std::set<Expression*> deadExprs_;
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // Phase 1: Compute demanded bits for all expressions
    void computeDemandedBits(std::vector<StmtPtr>& stmts);
    void computeDemandedBitsForStmt(Statement* stmt);
    void computeDemandedBitsForExpr(Expression* expr, DemandedBits demanded);
    
    // Phase 2: Identify dead/simplifiable expressions
    void identifyDeadCode(std::vector<StmtPtr>& stmts);
    bool isExpressionDead(Expression* expr);
    
    // Phase 3: Transform/remove dead code
    void transformDeadCode(std::vector<StmtPtr>& stmts);
    ExprPtr simplifyExpression(Expression* expr);
    
    // Helper: Get demanded bits for binary operation operands
    DemandedBits getDemandedBitsForBinaryOp(TokenType op, DemandedBits resultDemanded,
                                            Expression* lhs, Expression* rhs, bool isLHS);
    
    // Helper: Get demanded bits for unary operation operand
    DemandedBits getDemandedBitsForUnaryOp(TokenType op, DemandedBits resultDemanded);
    
    // Helper: Check if expression has side effects
    bool hasSideEffects(Expression* expr);
    
    // Helper: Get bit width for a type
    int getBitWidth(Expression* expr);
    
    // Helper: Convert sext to zext if high bits not demanded
    ExprPtr convertSExtToZExt(Expression* expr, DemandedBits demanded);
    
    // Helper: Simplify AND/OR/XOR when mask doesn't affect demanded bits
    ExprPtr simplifyBitwiseOp(BinaryExpr* expr, DemandedBits demanded);
};

} // namespace tyl

#endif // TYL_BDCE_H
