# Tyl Compiler Optimization Analysis & Implementation Plan

## Current Tyl Compiler Optimization Inventory

### ✅ Already Implemented (24+ passes)

| Pass | LLVM Equivalent | Status | Quality |
|------|-----------------|--------|---------|
| Constant Folding | `instcombine` (partial) | ✅ Complete | Good |
| Constant Propagation | `sccp` | ✅ Complete | Good |
| Dead Code Elimination | `dce`, `adce` | ✅ Complete | Excellent |
| Dead Store Elimination | `dse` | ✅ Complete | Excellent |
| Common Subexpression Elimination | `early-cse` | ✅ Complete | Good |
| Global Value Numbering | `gvn`, `newgvn` | ✅ Complete | Good |
| GVN-PRE | `gvn` (PRE) | ✅ Complete | Good |
| Copy Propagation | Part of GVN | ✅ Complete | Good |
| Algebraic Simplification | `instcombine` | ✅ Complete | Good |
| Loop Unrolling | `loop-unroll` | ✅ Complete | Excellent |
| Loop Invariant Code Motion | `licm` | ✅ Complete | Good |
| Enhanced LICM | `licm` (advanced) | ✅ Complete | Good |
| Strength Reduction | `loop-reduce`, `slsr` | ✅ Complete | Good |
| Function Inlining | `inline` | ✅ Complete | Excellent |
| Tail Call Optimization | `tailcallelim` | ✅ Complete | Excellent |
| CTFE | N/A (Tyl-specific) | ✅ Complete | Excellent |
| SSA Construction | Core LLVM IR | ✅ Complete | Good |
| Instruction Scheduling | CodeGen phase | ✅ Complete | Excellent |
| PGO | `pgo-instr-*` | ✅ Complete | Excellent |
| Compile-Time Reflection | N/A (Tyl-specific) | ✅ Complete | Excellent |
| SimplifyCFG | `simplifycfg` | ✅ Complete | Good |
| ADCE | `adce` | ✅ Complete | Good |
| Reassociate | `reassociate` | ✅ Complete | Good |
| SROA | `sroa` | ✅ Complete | Good |
| mem2reg | `mem2reg` | ✅ Complete | Good |

### Recent Completions (January 2026)

- **Reassociate**: Expression reassociation (`scalar/reassociate.h/cpp`) - reorders commutative/associative ops for constant folding
- **SROA**: Scalar Replacement of Aggregates (`scalar/sroa.h/cpp`) - breaks up records into scalar variables
- **mem2reg**: Memory to Register Promotion (`scalar/mem2reg.h/cpp`) - promotes stack allocations to SSA registers
- **SimplifyCFG**: Control flow graph simplification (`cfg/simplify_cfg.h/cpp`) - merges blocks, removes unreachable code, simplifies constant conditions
- **ADCE**: Aggressive Dead Code Elimination (`scalar/adce.h/cpp`) with liveness analysis - enabled at O3/Ofast
- **GVN-PRE**: Enhanced GVN with Partial Redundancy Elimination (`scalar/gvn_pre.h/cpp`) - constant folding and load optimization
- **Enhanced LICM**: More aggressive loop invariant code motion (`loop/enhanced_licm.h/cpp`) with alias analysis
- **Dead Store Elimination**: Standalone pass (`scalar/dead_store.h/cpp`) with proper liveness analysis, escape detection
- **Loop Unrolling**: Enhanced with partial unrolling support - generates unrolled main loop with remainder iterations
- **Instruction Scheduling**: Fixed bug with AssignExpr handling for compound assignments
- **PGO**: Complete implementation with profile collection, instrumentation, branch reordering, loop unroll hints

---

## LLVM Universal Optimization Passes Analysis

### HIGH PRIORITY - Recommended for Implementation

#### 1. **SimplifyCFG** (Control Flow Graph Simplification) ✅ IMPLEMENTED
- **LLVM Pass**: `simplifycfg`
- **What it does**: Merges basic blocks, removes unreachable blocks, converts switches to lookup tables, hoists/sinks common instructions
- **Why implement**: Cleans up control flow after other optimizations, enables further optimizations
- **Complexity**: Medium
- **Impact**: High - foundational pass that enables many other optimizations
- **Status**: ✅ Complete - `cfg/simplify_cfg.h/cpp`

#### 2. **Reassociate** (Expression Reassociation) ✅ IMPLEMENTED
- **LLVM Pass**: `reassociate`
- **What it does**: Reorders commutative/associative operations to expose more constant folding and CSE opportunities
- **Example**: `(a + 1) + 2` → `a + 3`, `(a * b) * a` → `(a * a) * b`
- **Why implement**: Enables more constant folding, works well with your existing algebraic simplification
- **Complexity**: Medium
- **Impact**: Medium-High
- **Status**: ✅ Complete - `scalar/reassociate.h/cpp`

#### 3. **SROA** (Scalar Replacement of Aggregates) ✅ IMPLEMENTED
- **LLVM Pass**: `sroa`
- **What it does**: Breaks up aggregates (structs, arrays) into individual scalar variables
- **Why implement**: Critical for records/tuples in Tyl, enables register allocation
- **Complexity**: Medium-High
- **Impact**: High for struct-heavy code
- **Status**: ✅ Complete - `scalar/sroa.h/cpp`

#### 4. **mem2reg** (Memory to Register Promotion) ✅ IMPLEMENTED
- **LLVM Pass**: `mem2reg`
- **What it does**: Promotes stack allocations to SSA registers when possible
- **Why implement**: Fundamental for good code generation, reduces memory traffic
- **Complexity**: Medium (you have SSA infrastructure)
- **Impact**: Very High
- **Status**: ✅ Complete - `scalar/mem2reg.h/cpp`

#### 5. **Jump Threading** ✅ IMPLEMENTED
- **LLVM Pass**: `jump-threading`
- **What it does**: Threads jumps through blocks with predictable conditions
- **Example**: If block A jumps to B, and B's condition is known from A, skip B
- **Why implement**: Reduces branch mispredictions, simplifies control flow
- **Complexity**: Medium
- **Impact**: Medium-High
- **Status**: ✅ Complete - `cfg/jump_threading.h/cpp`

#### 6. **Loop Rotation** ✅ IMPLEMENTED
- **LLVM Pass**: `loop-rotate`
- **What it does**: Transforms loops to have exit condition at bottom (do-while form)
- **Why implement**: Enables better LICM, loop unrolling, and vectorization
- **Complexity**: Medium
- **Impact**: Medium-High (prerequisite for advanced loop opts)
- **Status**: ✅ Complete - `loop/loop_rotation.h/cpp`

#### 7. **Induction Variable Simplification** ✅ IMPLEMENTED
- **LLVM Pass**: `indvars`
- **What it does**: Canonicalizes induction variables, computes trip counts
- **Why implement**: Enables better loop unrolling decisions, strength reduction
- **Complexity**: Medium
- **Impact**: Medium-High
- **Status**: ✅ Complete - `loop/indvar_simplify.h/cpp`

---

### MEDIUM PRIORITY - Good to Have

#### 8. **Loop Deletion**
- **LLVM Pass**: `loop-deletion`
- **What it does**: Removes loops that have no side effects and unused results
- **Complexity**: Low
- **Impact**: Low-Medium
- **Recommendation**: ⭐⭐⭐ IMPLEMENT (easy win)

#### 9. **Loop Idiom Recognition**
- **LLVM Pass**: `loop-idiom`
- **What it does**: Recognizes loops that implement memset/memcpy patterns
- **Example**: `for(i=0;i<n;i++) a[i]=0` → `memset(a, 0, n)`
- **Complexity**: Medium
- **Impact**: High for array-heavy code
- **Recommendation**: ⭐⭐⭐ IMPLEMENT

#### 10. **MemCpyOpt** (Memory Copy Optimization)
- **LLVM Pass**: `memcpyopt`
- **What it does**: Optimizes memory operations, merges adjacent stores
- **Complexity**: Medium
- **Impact**: Medium
- **Recommendation**: ⭐⭐⭐ IMPLEMENT

#### 11. **ADCE** (Aggressive Dead Code Elimination) ✅ IMPLEMENTED
- **LLVM Pass**: `adce`
- **What it does**: More aggressive DCE that removes code based on liveness
- **Why implement**: Your DCE is good but ADCE catches more cases
- **Complexity**: Medium
- **Impact**: Medium
- **Status**: ✅ Complete - `scalar/adce.h/cpp` (enabled at O3+)

#### 12. **BDCE** (Bit-Tracking Dead Code Elimination)
- **LLVM Pass**: `bdce`
- **What it does**: Removes code that computes bits that are never used
- **Example**: If only low 8 bits of a 32-bit value are used, simplify computation
- **Complexity**: Medium-High
- **Impact**: Medium
- **Recommendation**: ⭐⭐⭐ CONSIDER

#### 13. **Correlated Value Propagation**
- **LLVM Pass**: `correlated-propagation`
- **What it does**: Uses range analysis to simplify comparisons and eliminate branches
- **Example**: After `if (x > 0)`, knows `x > 0` in that branch
- **Complexity**: Medium
- **Impact**: Medium
- **Recommendation**: ⭐⭐⭐ IMPLEMENT

#### 14. **Constraint Elimination**
- **LLVM Pass**: `constraint-elimination`
- **What it does**: Uses constraint solving to eliminate redundant checks
- **Why implement**: Works well with Tyl's refinement types
- **Complexity**: High
- **Impact**: Medium-High for bounds checking
- **Recommendation**: ⭐⭐⭐ CONSIDER

---

### LOWER PRIORITY - Advanced/Specialized

#### 15. **Loop Vectorization**
- **LLVM Pass**: `loop-vectorize`
- **What it does**: Auto-vectorizes loops using SIMD instructions
- **Complexity**: Very High
- **Impact**: Very High for numeric code
- **Recommendation**: ⭐⭐ FUTURE (requires target-specific codegen)

#### 16. **SLP Vectorization**
- **LLVM Pass**: `slp-vectorizer`
- **What it does**: Vectorizes straight-line code (non-loop)
- **Complexity**: Very High
- **Impact**: Medium-High
- **Recommendation**: ⭐⭐ FUTURE

#### 17. **Loop Fusion**
- **LLVM Pass**: `loop-fusion`
- **What it does**: Merges adjacent loops with same bounds
- **Complexity**: High
- **Impact**: Medium
- **Recommendation**: ⭐⭐ FUTURE

#### 18. **Loop Interchange**
- **LLVM Pass**: `loop-interchange`
- **What it does**: Swaps nested loop order for better cache locality
- **Complexity**: High
- **Impact**: High for matrix operations
- **Recommendation**: ⭐⭐ FUTURE

#### 19. **Hot/Cold Splitting**
- **LLVM Pass**: `hotcoldsplit`
- **What it does**: Moves cold code to separate functions
- **Complexity**: Medium
- **Impact**: Medium (code size, I-cache)
- **Recommendation**: ⭐⭐ FUTURE

#### 20. **Function Merging**
- **LLVM Pass**: `mergefunc`
- **What it does**: Merges identical functions
- **Complexity**: Medium
- **Impact**: Low-Medium (code size)
- **Recommendation**: ⭐⭐ FUTURE

---

## IPO (Inter-Procedural Optimization) Passes

#### 21. **Dead Argument Elimination**
- **LLVM Pass**: `deadargelim`
- **What it does**: Removes unused function arguments
- **Complexity**: Low-Medium
- **Impact**: Low-Medium
- **Recommendation**: ⭐⭐⭐ IMPLEMENT

#### 22. **Argument Promotion**
- **LLVM Pass**: `argpromotion`
- **What it does**: Promotes pointer arguments to pass-by-value when beneficial
- **Complexity**: Medium
- **Impact**: Medium
- **Recommendation**: ⭐⭐⭐ CONSIDER

#### 23. **Global DCE**
- **LLVM Pass**: `globaldce`
- **What it does**: Removes unused global variables and functions
- **Complexity**: Low
- **Impact**: Medium (code size)
- **Recommendation**: ⭐⭐⭐ IMPLEMENT (you have tree shaking, enhance it)

#### 24. **GlobalOpt**
- **LLVM Pass**: `globalopt`
- **What it does**: Optimizes global variables (constify, internalize, etc.)
- **Complexity**: Medium
- **Impact**: Medium
- **Recommendation**: ⭐⭐⭐ CONSIDER

#### 25. **IPSCCP** (Inter-Procedural SCCP)
- **LLVM Pass**: `ipsccp`
- **What it does**: Constant propagation across function boundaries
- **Complexity**: Medium-High
- **Impact**: Medium-High
- **Recommendation**: ⭐⭐⭐ IMPLEMENT

---

## Verification of Current Tyl Passes

### ✅ EXCELLENT - Fully Implemented
- **Constant Folding**: Comprehensive, handles all operators
- **Function Inlining**: Well-designed with complexity analysis, pure function detection
- **Tail Call Optimization**: Correct implementation with loop transformation
- **CTFE**: Excellent, unique to Tyl, well-integrated with reflection
- **PGO**: Good infrastructure for profile-guided decisions
- **Dead Code Elimination**: Enhanced with ADCE-style liveness analysis (O3+) ✅
- **Loop Unrolling**: Partial unrolling for non-constant bounds implemented ✅
- **LICM**: Enhanced with alias analysis via EnhancedLICMPass (O3+) ✅
- **GVN**: Enhanced with GVN-PRE for load/store optimization ✅
- **SimplifyCFG**: Complete with constant condition elimination, dead branch removal ✅
- **Dead Store Elimination**: Standalone pass with liveness analysis ✅

### 🔧 COMPLETED ENHANCEMENTS (January 2026)
- **SimplifyCFG**: Control flow simplification (`cfg/simplify_cfg.h/cpp`) ✅
- **Dead Store Elimination**: Standalone pass with proper liveness analysis ✅
- **Loop Unrolling**: Added partial unrolling with remainder loop support ✅
- **Instruction Scheduling**: Fixed AssignExpr bug, fully integrated at O3/Ofast ✅
- **PGO**: Complete with instrumentation, profile loading, and optimization hints ✅
- **ADCE**: Aggressive Dead Code Elimination with liveness analysis (O3+) ✅
- **Enhanced LICM**: More aggressive hoisting with alias analysis (O3+) ✅
- **GVN-PRE**: Global Value Numbering with Partial Redundancy Elimination ✅
- **Reassociate**: Expression reassociation for constant folding ✅
- **SROA**: Scalar Replacement of Aggregates ✅
- **mem2reg**: Memory to Register Promotion ✅

### 📁 New Files Added (January 2026)
- `semantic/optimizer/cfg/simplify_cfg.h/cpp` - CFG simplification pass
- `semantic/optimizer/scalar/adce.h/cpp` - ADCE pass with liveness analysis
- `semantic/optimizer/scalar/gvn_pre.h/cpp` - Enhanced GVN with PRE and load/store optimization
- `semantic/optimizer/loop/enhanced_licm.h/cpp` - Enhanced LICM with alias analysis
- `semantic/optimizer/scalar/reassociate.h/cpp` - Expression reassociation pass
- `semantic/optimizer/scalar/sroa.h/cpp` - Scalar Replacement of Aggregates pass
- `semantic/optimizer/scalar/mem2reg.h/cpp` - Memory to Register Promotion pass

---

## Recommended Implementation Order

### Phase 1: Foundation (v1.1) - COMPLETE ✅
1. ✅ **SimplifyCFG** - Complete (`cfg/simplify_cfg.h/cpp`)
2. ✅ **mem2reg** - Complete (`scalar/mem2reg.h/cpp`)
3. ✅ **Reassociate** - Complete (`scalar/reassociate.h/cpp`)
4. **Loop Rotation** - Prerequisite for advanced loop opts

### Phase 2: Loop Enhancements (v1.2)
5. **Induction Variable Simplification**
6. **Loop Deletion**
7. **Loop Idiom Recognition**
8. ✅ **Dead Store Elimination** - Complete (`scalar/dead_store.h/cpp`)

### Phase 3: Advanced Scalar (v1.3) - MOSTLY COMPLETE
9. **Jump Threading**
10. ✅ **SROA** - Complete (`scalar/sroa.h/cpp`)
11. **Correlated Value Propagation**
12. ✅ **ADCE** - Complete (`scalar/adce.h/cpp`)

### Phase 4: IPO (v1.4)
13. **Dead Argument Elimination**
14. **IPSCCP**
15. **Enhanced Global DCE**

### Phase 5: Vectorization (v2.0)
16. **Loop Vectorization** (requires SIMD codegen)
17. **SLP Vectorization**

---

## Summary

**Current State**: Your tyl compiler has a solid optimization foundation with 24+ passes covering the essential optimizations. The implementation quality is excellent, especially for inlining, TCO, CTFE, and the recently added passes.

**Recently Completed**:
1. ✅ Reassociate (expression reordering for constant folding)
2. ✅ SROA (aggregate decomposition into scalars)
3. ✅ mem2reg (memory to register promotion)
4. ✅ SimplifyCFG (control flow cleanup)
5. ✅ ADCE (aggressive dead code elimination)
6. ✅ GVN-PRE (partial redundancy elimination)
7. ✅ Enhanced LICM (with alias analysis)
8. ✅ Dead Store Elimination (standalone pass)
9. ✅ Instruction Scheduler bug fix (AssignExpr handling)

**Key Remaining Gaps**:
1. No Jump Threading
2. No Loop Rotation
3. No Induction Variable Simplification
4. Limited IPO beyond inlining

**Next Step**: Implement **Jump Threading** or **Loop Rotation** - both are medium complexity and will improve performance.

**Passes to Skip**: Loop vectorization and SLP vectorization require significant target-specific work. Save these for when you have a more mature backend.


---

## Detailed Implementation Specifications

### 1. SimplifyCFG Pass

```cpp
// semantic/optimizer/simplify_cfg.h
class SimplifyCFGPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "SimplifyCFG"; }
    
private:
    // Merge sequential blocks with single predecessor/successor
    bool mergeBlocks(std::vector<StmtPtr>& stmts);
    
    // Remove empty blocks (just jumps)
    bool removeEmptyBlocks(std::vector<StmtPtr>& stmts);
    
    // Convert if-else chains to switch when beneficial
    bool convertIfToSwitch(IfStmt* ifStmt);
    
    // Hoist common code from if/else branches
    bool hoistCommonCode(IfStmt* ifStmt);
    
    // Sink common code to after if/else
    bool sinkCommonCode(IfStmt* ifStmt);
    
    // Simplify: if (true) A else B → A
    bool simplifyConstantConditions(StmtPtr& stmt);
    
    // Thread jumps: if (x) goto L1; L1: if (x) ... → if (x) ...
    bool threadJumps(std::vector<StmtPtr>& stmts);
};
```

### 2. mem2reg Pass

```cpp
// semantic/optimizer/mem2reg.h
class Mem2RegPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "Mem2Reg"; }
    
private:
    // Identify promotable allocations (no address taken, simple types)
    std::set<std::string> findPromotableVars(FnDecl* fn);
    
    // Build dominance frontier for phi placement
    void computeDominanceFrontier(FnDecl* fn);
    
    // Insert phi nodes at dominance frontiers
    void insertPhiNodes(FnDecl* fn, const std::set<std::string>& vars);
    
    // Rename variables to SSA form
    void renameVariables(FnDecl* fn);
    
    // Remove original allocations
    void removePromotedAllocations(FnDecl* fn);
};
```

### 3. Reassociate Pass

```cpp
// semantic/optimizer/reassociate.h
class ReassociatePass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "Reassociate"; }
    
private:
    // Linearize expression tree for commutative ops
    void linearize(BinaryExpr* expr, std::vector<ExprPtr>& operands);
    
    // Sort operands: constants last, then by rank
    void sortOperands(std::vector<ExprPtr>& operands);
    
    // Rebuild balanced tree from sorted operands
    ExprPtr rebuildTree(std::vector<ExprPtr>& operands, TokenType op);
    
    // Compute rank for operand ordering
    int computeRank(Expression* expr);
    
    // Handle: (a + C1) + C2 → a + (C1 + C2)
    bool foldConstants(std::vector<ExprPtr>& operands, TokenType op);
};
```

### 4. SROA Pass

```cpp
// semantic/optimizer/sroa.h
class SROAPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "SROA"; }
    
private:
    // Analyze if aggregate can be split
    bool canSplit(VarDecl* var);
    
    // Create scalar replacements for each field
    std::map<std::string, VarDecl*> createScalars(VarDecl* var);
    
    // Rewrite field accesses to use scalars
    void rewriteAccesses(FnDecl* fn, VarDecl* var, 
                         const std::map<std::string, VarDecl*>& scalars);
    
    // Handle nested aggregates recursively
    void splitNested(VarDecl* var);
};
```

### 5. Loop Rotation Pass

```cpp
// semantic/optimizer/loop_rotation.h
class LoopRotationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopRotation"; }
    
private:
    // Transform: while(cond) { body } 
    // To: if(cond) { do { body } while(cond) }
    StmtPtr rotateWhileLoop(WhileStmt* loop);
    
    // Transform for loops similarly
    StmtPtr rotateForLoop(ForStmt* loop);
    
    // Check if rotation is profitable
    bool shouldRotate(Statement* loop);
    
    // Clone loop header for rotation
    StmtPtr cloneHeader(Statement* header);
};
```

---

## Updated Pipeline Recommendation

```cpp
void Optimizer::optimize(Program& ast) {
    // PHASE 0: PGO (if available)
    if (pgoEnabled_) runPGO(ast);
    
    // PHASE 1: Early Simplification
    runSimplifyConstantConditions(ast);  // NEW
    runConstantFolding(ast);
    runCTFE(ast);
    
    // PHASE 2: Canonicalization
    runReassociate(ast);                 // NEW
    runAlgebraicSimplification(ast);
    runConstantFolding(ast);
    
    // PHASE 3: Memory Optimization
    runMem2Reg(ast);                     // NEW
    runSROA(ast);                        // NEW
    
    // PHASE 4: Loop Preparation
    runLoopRotation(ast);                // NEW
    runIndVarSimplify(ast);              // NEW
    
    // PHASE 5: Loop Optimization
    runLICM(ast);
    runLoopUnrolling(ast);
    runLoopDeletion(ast);                // NEW
    runLoopIdiomRecognize(ast);          // NEW
    
    // PHASE 6: Scalar Optimization
    runCSE(ast);
    runGVN(ast);
    runJumpThreading(ast);               // NEW
    runCorrelatedPropagation(ast);       // NEW
    
    // PHASE 7: Function Optimization
    runInlining(ast);
    runTailCallOptimization(ast);
    
    // PHASE 8: IPO
    runDeadArgElim(ast);                 // NEW
    runIPSCCP(ast);                      // NEW
    
    // PHASE 9: Cleanup
    runSimplyCFG(ast);                   // NEW
    runADCE(ast);                        // NEW
    runDSE(ast);                         // NEW (separated)
    runConstantFolding(ast);             // Final cleanup
}
```

---

## Comparison with LLVM O2 Pipeline

| LLVM O2 Pass | Tyl Equivalent | Status |
|--------------|----------------|--------|
| `simplifycfg` | SimplifyCFG | ✅ Have |
| `sroa` | SROA | ✅ Have |
| `early-cse` | CSE | ✅ Have |
| `instcombine` | Algebraic + ConstFold | ✅ Have |
| `reassociate` | Reassociate | ✅ Have |
| `mem2reg` | Mem2Reg | ✅ Have |
| `loop-simplify` | - | ❌ Missing |
| `loop-rotate` | - | ❌ Missing |
| `licm` | LICM + EnhancedLICM | ✅ Have |
| `indvars` | - | ❌ Missing |
| `loop-idiom` | - | ❌ Missing |
| `loop-deletion` | - | ❌ Missing |
| `loop-unroll` | LoopUnrolling | ✅ Have |
| `gvn` | GVN + GVN-PRE | ✅ Have |
| `memcpyopt` | - | ❌ Missing |
| `sccp` | ConstProp | ✅ Have |
| `dce` | DCE | ✅ Have |
| `dse` | DSE | ✅ Have |
| `adce` | ADCE | ✅ Have |
| `inline` | Inlining | ✅ Have |
| `tailcallelim` | TCO | ✅ Have |

**Coverage**: 16/21 core O2 passes (76%)
**With recommended additions**: 20/21 (95%)
