# Tyl Compiler Optimization Analysis & Implementation Plan

## Implementation Status

### Bug Fixes

**Global Variable Register Allocation Fix** (Latest)
- Fixed a critical bug where function-level register allocation could clobber registers used by global variables
- The issue occurred because both global and function-level allocators used the same callee-saved registers (RBX, R12-R15)
- Solution: Function-level allocator now receives a set of reserved registers (those used by globals) and avoids using them
- This fix ensures correct behavior at all optimization levels (O0-O3)

**IPSCCP Loop Variable Fix** (Latest)
- Fixed a bug where IPSCCP incorrectly treated loop-modified variables as constants
- The issue was that `ExprStmt` containing `AssignExpr` (e.g., `i = i + 1`) was not being detected as an assignment
- Solution: Added handling for `AssignExpr` inside `ExprStmt` in `markLoopModifiedVariablesAsTop`

---

## Complete Optimization Pass Inventory

### Scalar Optimizations (`scalar/`)

| Pass | File | Status | Opt Level | Description |
|------|------|--------|-----------|-------------|
| Constant Folding | `constant_folding.h/cpp` | ✅ Complete | O1+ | Evaluates constant expressions at compile time |
| Constant Propagation | `constant_propagation.h/cpp` | ✅ Complete | O1+ | Propagates known constant values through code |
| Dead Code Elimination | `dead_code.h/cpp` | ✅ Complete | O1+ | Removes unreachable and unused code |
| Dead Store Elimination | `dead_store.h/cpp` | ✅ Complete | O1+ | Removes stores to variables that are never read |
| CSE | `cse.h/cpp` | ✅ Complete | O2+ | Common Subexpression Elimination |
| GVN | `gvn.h/cpp` | ✅ Complete | O2+ | Global Value Numbering |
| GVN-PRE | `gvn_pre.h/cpp` | ✅ Complete | O3+ | GVN with Partial Redundancy Elimination |
| Algebraic Simplification | `algebraic.h/cpp` | ✅ Complete | O3+ | Algebraic identity simplifications |
| Reassociate | `reassociate.h/cpp` | ✅ Complete | O2+ | Reorders expressions for better constant folding |
| SROA | `sroa.h/cpp` | ✅ Complete | O2+ | Scalar Replacement of Aggregates |
| mem2reg | `mem2reg.h/cpp` | ✅ Complete | O2+ | Memory to Register Promotion |
| MemCpyOpt | `memcpyopt.h/cpp` | ✅ Complete | O2+ | Merges adjacent stores into memset/memcpy |
| ADCE | `adce.h/cpp` | ✅ Complete | O3+ | Aggressive Dead Code Elimination |
| BDCE | `bdce.h/cpp` | ✅ Complete | O3+ | Bit-Tracking Dead Code Elimination |
| Correlated Value Propagation | `correlated_propagation.h/cpp` | ✅ Complete | O2+ | Range-based comparison simplification |
| Constraint Elimination | `constraint_elimination.h/cpp` | ✅ Complete | O3+ | Constraint solving for redundant checks |

### Loop Optimizations (`loop/`)

| Pass | File | Status | Opt Level | Description |
|------|------|--------|-----------|-------------|
| Loop Optimizer | `loop_optimizer.h/cpp` | ✅ Complete | O2+ | Main loop optimization driver (LICM, unrolling, strength reduction) |
| Enhanced LICM | `enhanced_licm.h/cpp` | ✅ Complete | O3+ | Advanced Loop Invariant Code Motion with alias analysis |
| Loop Rotation | `loop_rotation.h/cpp` | ✅ Complete | O2+ | Transforms loops to have exit at bottom |
| IndVar Simplify | `indvar_simplify.h/cpp` | ✅ Complete | O2+ | Induction Variable canonicalization |
| Loop Deletion | `loop_deletion.h/cpp` | ✅ Complete | O2+ | Removes loops with no side effects |
| Loop Idiom | `loop_idiom.h/cpp` | ✅ Complete | O2+ | Recognizes memset/memcpy patterns |
| Loop Simplify | `loop_simplify.h/cpp` | ✅ Complete | O2+ | Loop canonicalization (preheaders, single latch) |

### CFG Optimizations (`cfg/`)

| Pass | File | Status | Opt Level | Description |
|------|------|--------|-----------|-------------|
| SimplifyCFG | `simplify_cfg.h/cpp` | ✅ Complete | O1+ | Control flow graph simplification |
| Jump Threading | `jump_threading.h/cpp` | ✅ Complete | O2+ | Threads jumps through predictable conditions |

### Function Optimizations (`function/`)

| Pass | File | Status | Opt Level | Description |
|------|------|--------|-----------|-------------|
| Inlining | `inlining.h/cpp` | ✅ Complete | O2+ | Function inlining with cost analysis |
| Tail Call | `tail_call.h/cpp` | ✅ Complete | O2+ | Tail call optimization |
| CTFE | `ctfe.h/cpp` | ✅ Complete | O2+ | Compile-Time Function Evaluation |

### IPO (Inter-Procedural Optimization) (`ipo/`)

| Pass | File | Status | Opt Level | Description |
|------|------|--------|-----------|-------------|
| IPSCCP | `ipsccp.h/cpp` | ✅ Complete | O2+ | Inter-Procedural Sparse Conditional Constant Propagation |
| Dead Argument Elimination | `dead_arg_elim.h/cpp` | ✅ Complete | O2+ | Removes unused function arguments |
| GlobalOpt | `global_opt.h/cpp` | ✅ Complete | O2+ | Global variable optimization (constify, eliminate unused) |
| Partial Inlining | `partial_inlining.h/cpp` | ✅ Complete | O3+ | Inlines hot paths, keeps cold paths as calls |
| Speculative Devirtualization | `speculative_devirt.h/cpp` | ✅ Complete | O2+ | Converts virtual calls to direct calls |

### Analysis Passes (`analysis/`)

| Pass | File | Status | Opt Level | Description |
|------|------|--------|-----------|-------------|
| SSA | `ssa.h/cpp` | ✅ Complete | O3+ | SSA form construction |
| Instruction Scheduler | `instruction_scheduler.h/cpp` | ✅ Complete | O3+ | Instruction scheduling for ILP |
| PGO | `pgo.h/cpp` | ✅ Complete | Ofast | Profile-Guided Optimization |

---

## Optimization Level Summary

### O0 - No Optimization
- No passes enabled
- Fastest compile time, best for debugging

### O1 - Basic Optimization
- Constant Folding
- Dead Code Elimination
- SimplifyCFG

### O2 - Standard Optimization
All O1 passes plus:
- Constant Propagation
- Dead Store Elimination
- CSE, GVN
- Reassociate, SROA, mem2reg, MemCpyOpt
- Loop Optimizer (LICM, unrolling, strength reduction)
- Loop Rotation, IndVar Simplify, Loop Deletion, Loop Idiom, Loop Simplify
- Jump Threading
- Inlining, Tail Call, CTFE
- IPSCCP, Dead Argument Elimination, GlobalOpt, Speculative Devirtualization
- Correlated Value Propagation

### O3 - Aggressive Optimization
All O2 passes plus:
- Algebraic Simplification
- GVN-PRE
- Enhanced LICM
- ADCE, BDCE
- Constraint Elimination
- Partial Inlining
- SSA, Instruction Scheduler

### Ofast - Maximum Optimization
All O3 passes plus:
- PGO (Profile-Guided Optimization)
- More aggressive inlining thresholds

---

## Pass Count Summary

| Category | Count |
|----------|-------|
| Scalar | 16 |
| Loop | 7 |
| CFG | 2 |
| Function | 3 |
| IPO | 5 |
| Analysis | 3 |
| **Total** | **36** |

---

## LLVM O2 Coverage

| LLVM O2 Pass | Tyl Equivalent | Status |
|--------------|----------------|--------|
| `simplifycfg` | SimplifyCFG | ✅ |
| `sroa` | SROA | ✅ |
| `early-cse` | CSE | ✅ |
| `instcombine` | Algebraic + ConstFold | ✅ |
| `reassociate` | Reassociate | ✅ |
| `mem2reg` | Mem2Reg | ✅ |
| `loop-simplify` | Loop Simplify | ✅ |
| `loop-rotate` | LoopRotation | ✅ |
| `licm` | LICM + EnhancedLICM | ✅ |
| `indvars` | IndVarSimplify | ✅ |
| `loop-idiom` | LoopIdiomRecognition | ✅ |
| `loop-deletion` | LoopDeletion | ✅ |
| `loop-unroll` | LoopUnrolling | ✅ |
| `gvn` | GVN + GVN-PRE | ✅ |
| `memcpyopt` | MemCpyOpt | ✅ |
| `sccp` | ConstProp | ✅ |
| `ipsccp` | IPSCCP | ✅ |
| `dce` | DCE | ✅ |
| `dse` | DSE | ✅ |
| `adce` | ADCE | ✅ |
| `inline` | Inlining | ✅ |
| `tailcallelim` | TCO | ✅ |
| `deadargelim` | DeadArgElim | ✅ |
| `globalopt` | GlobalOpt | ✅ |

**Coverage**: 24/24 core O2 passes (100%)

---

## Future Work (Not Implemented)

| Pass | Priority | Reason |
|------|----------|--------|
| Loop Vectorization | Low | Requires SIMD codegen |
| SLP Vectorization | Low | Requires SIMD codegen |
| Loop Fusion | Medium | Complex analysis |
| Loop Interchange | Medium | Cache optimization |
| Hot/Cold Splitting | Low | Code layout optimization |
| Function Merging | Low | Code size optimization |
| Argument Promotion | Medium | Pointer to value conversion |

These passes are deferred until multi-platform backend support is implemented.
