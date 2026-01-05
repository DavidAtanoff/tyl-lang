// Tyl Compiler - SIMD Vectorizer
// Auto-vectorization for loops and array operations
#ifndef TYL_VECTORIZER_H
#define TYL_VECTORIZER_H

#include "frontend/ast/ast.h"
#include <vector>
#include <string>
#include <set>

namespace tyl {

// Forward declarations
class X64Assembler;
class NativeCodeGen;

// Vectorization width (number of elements processed in parallel)
enum class VectorWidth {
    SCALAR = 1,     // No vectorization
    SSE_2 = 2,      // SSE: 2 doubles or 4 floats
    SSE_4 = 4,      // SSE: 4 ints or 4 floats
    AVX_4 = 4,      // AVX: 4 doubles
    AVX_8 = 8       // AVX: 8 floats or 8 ints
};

// Information about a vectorizable loop
struct VectorizableLoop {
    ForStmt* loop;
    std::string inductionVar;       // Loop counter variable
    int64_t tripCount;              // Number of iterations (if known)
    bool tripCountKnown;
    VectorWidth width;              // Recommended vector width
    
    // Loop body analysis
    bool hasReduction;              // e.g., sum += arr[i]
    std::string reductionVar;
    TokenType reductionOp;          // PLUS, STAR, etc.
    
    bool hasArrayAccess;            // Accesses array with induction var
    std::string arrayVar;
    
    bool isVectorizable;            // Can this loop be vectorized?
    std::string reason;             // Why not vectorizable (if not)
};

// Vectorizer pass - analyzes and transforms loops for SIMD
class Vectorizer {
public:
    Vectorizer();
    
    // Analyze program for vectorization opportunities
    void analyze(Program& program);
    
    // Get vectorizable loops found
    const std::vector<VectorizableLoop>& getVectorizableLoops() const { return loops_; }
    
    // Check if a specific loop can be vectorized
    bool canVectorize(ForStmt* loop);
    
    // Get recommended vector width for a loop
    VectorWidth getRecommendedWidth(ForStmt* loop);
    
    // Statistics
    int loopsAnalyzed() const { return loopsAnalyzed_; }
    int loopsVectorizable() const { return loopsVectorizable_; }
    
private:
    std::vector<VectorizableLoop> loops_;
    int loopsAnalyzed_ = 0;
    int loopsVectorizable_ = 0;
    
    // Analysis helpers
    void analyzeLoop(ForStmt* loop);
    bool analyzeLoopBody(Statement* body, VectorizableLoop& info);
    bool checkDependencies(Statement* body, const std::string& inductionVar);
    bool isSimpleReduction(Statement* stmt, VectorizableLoop& info);
    bool isArrayAccess(Expression* expr, const std::string& inductionVar, std::string& arrayName);
    int64_t getTripCount(ForStmt* loop);
};

// SIMD Code Generator - generates vectorized code
class SIMDCodeGen {
public:
    // Check if SSE is available (always true on x64)
    static bool hasSSE() { return true; }
    
    // Check if AVX is available (runtime check needed)
    static bool hasAVX();
    
    // Get optimal vector width for current CPU
    static VectorWidth getOptimalWidth();
    
    // Generate vectorized loop code
    // Returns true if vectorization was successful
    bool generateVectorizedLoop(const VectorizableLoop& loop, class X64Assembler& asm_,
                                class NativeCodeGen& codegen);
    
private:
    // Generate vectorized reduction (sum, product)
    void generateReduction(const VectorizableLoop& loop, X64Assembler& asm_,
                          NativeCodeGen& codegen);
    
    // Generate vectorized array operation
    void generateArrayOp(const VectorizableLoop& loop, X64Assembler& asm_,
                        NativeCodeGen& codegen);
    
    // Generate horizontal reduction to get final scalar result
    void generateHorizontalReduction(TokenType op, VectorWidth width, X64Assembler& asm_);
};

} // namespace tyl

#endif // TYL_VECTORIZER_H
