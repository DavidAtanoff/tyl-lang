// Tyl Compiler - Optimizer Implementation
// Complete Tier 2-5 Optimization Pipeline
#include "optimizer.h"

// Scalar optimizations
#include "scalar/constant_folding.h"
#include "scalar/constant_propagation.h"
#include "scalar/dead_code.h"
#include "scalar/dead_store.h"
#include "scalar/cse.h"
#include "scalar/gvn.h"
#include "scalar/algebraic.h"
#include "scalar/adce.h"
#include "scalar/gvn_pre.h"
#include "scalar/reassociate.h"
#include "scalar/sroa.h"
#include "scalar/mem2reg.h"
#include "scalar/memcpyopt.h"
#include "scalar/bdce.h"
#include "scalar/correlated_propagation.h"
#include "scalar/constraint_elimination.h"

// Loop optimizations
#include "loop/loop_optimizer.h"
#include "loop/enhanced_licm.h"
#include "loop/loop_rotation.h"
#include "loop/indvar_simplify.h"
#include "loop/loop_deletion.h"
#include "loop/loop_idiom.h"
#include "loop/loop_simplify.h"

// IPO (Inter-Procedural Optimization)
#include "ipo/ipsccp.h"
#include "ipo/dead_arg_elim.h"
#include "ipo/global_opt.h"
#include "ipo/partial_inlining.h"
#include "ipo/speculative_devirt.h"

// Function optimizations
#include "function/inlining.h"
#include "function/tail_call.h"
#include "function/ctfe.h"

// CFG optimizations
#include "cfg/simplify_cfg.h"
#include "cfg/jump_threading.h"

// Analysis passes
#include "analysis/ssa.h"
#include "analysis/instruction_scheduler.h"
#include "analysis/pgo.h"

#include <iostream>

namespace tyl {

Optimizer::Optimizer() {
    // Default: enable all Tier 2 passes at O2 level
    setOptLevel(OptLevel::O2);
}

void Optimizer::addPass(std::unique_ptr<OptimizationPass> pass) {
    passes_.push_back(std::move(pass));
}

void Optimizer::setOptLevel(OptLevel level) {
    optLevel_ = level;
    
    switch (level) {
        case OptLevel::O0:
            // No optimization - fastest compile, debug friendly
            constantFoldingEnabled_ = false;
            deadCodeEnabled_ = false;
            inliningEnabled_ = false;
            tailCallEnabled_ = false;
            ctfeEnabled_ = false;
            ssaEnabled_ = false;
            loopOptEnabled_ = false;
            schedulingEnabled_ = false;
            simplifyCFGEnabled_ = false;
            reassociateEnabled_ = false;
            sroaEnabled_ = false;
            mem2regEnabled_ = false;
            break;
            
        case OptLevel::O1:
            // Basic optimizations - constant folding, DCE, CFG simplification
            constantFoldingEnabled_ = true;
            deadCodeEnabled_ = true;
            inliningEnabled_ = false;
            tailCallEnabled_ = false;
            ctfeEnabled_ = false;
            ssaEnabled_ = false;
            loopOptEnabled_ = false;
            schedulingEnabled_ = false;
            simplifyCFGEnabled_ = true;  // Enable at O1
            reassociateEnabled_ = false;
            sroaEnabled_ = false;
            mem2regEnabled_ = false;
            break;
            
        case OptLevel::O2:
            // Moderate optimization - typical production level
            constantFoldingEnabled_ = true;
            deadCodeEnabled_ = true;
            inliningEnabled_ = true;
            tailCallEnabled_ = true;
            ctfeEnabled_ = true;
            ssaEnabled_ = false;
            loopOptEnabled_ = true;
            schedulingEnabled_ = false;
            simplifyCFGEnabled_ = true;
            reassociateEnabled_ = true;   // Enable at O2
            sroaEnabled_ = true;          // Enable at O2
            mem2regEnabled_ = true;       // Enable at O2
            aggressiveInlining_ = false;
            maxInlineStatements_ = 10;
            maxInlineCallCount_ = 5;
            break;
            
        case OptLevel::O3:
            // Aggressive optimization - vectorization, loop unrolling, more inlining
            constantFoldingEnabled_ = true;
            deadCodeEnabled_ = true;
            inliningEnabled_ = true;
            tailCallEnabled_ = true;
            ctfeEnabled_ = true;
            ssaEnabled_ = true;
            loopOptEnabled_ = true;
            schedulingEnabled_ = true;
            simplifyCFGEnabled_ = true;
            reassociateEnabled_ = true;
            sroaEnabled_ = true;
            mem2regEnabled_ = true;
            aggressiveInlining_ = true;
            maxInlineStatements_ = 50;
            maxInlineCallCount_ = 20;
            break;
            
        case OptLevel::Os:
            // Optimize for size - like O2 but prefers smaller code
            constantFoldingEnabled_ = true;
            deadCodeEnabled_ = true;
            inliningEnabled_ = true;
            tailCallEnabled_ = true;
            ctfeEnabled_ = true;
            ssaEnabled_ = false;
            loopOptEnabled_ = true;
            schedulingEnabled_ = false;
            simplifyCFGEnabled_ = true;
            reassociateEnabled_ = true;
            sroaEnabled_ = true;
            mem2regEnabled_ = true;
            aggressiveInlining_ = false;
            maxInlineStatements_ = 5;   // Less inlining for smaller code
            maxInlineCallCount_ = 3;
            break;
            
        case OptLevel::Oz:
            // Aggressive size optimization - minimal inlining
            constantFoldingEnabled_ = true;
            deadCodeEnabled_ = true;
            inliningEnabled_ = false;   // No inlining for smallest code
            tailCallEnabled_ = true;
            ctfeEnabled_ = true;
            ssaEnabled_ = false;
            loopOptEnabled_ = false;    // No loop unrolling
            schedulingEnabled_ = false;
            simplifyCFGEnabled_ = true;  // Still useful for size
            reassociateEnabled_ = true;  // Still useful for size
            sroaEnabled_ = true;         // Helps with register allocation
            mem2regEnabled_ = true;      // Reduces memory traffic
            aggressiveInlining_ = false;
            maxInlineStatements_ = 0;
            maxInlineCallCount_ = 0;
            break;
            
        case OptLevel::Ofast:
            // Like O3 plus unsafe optimizations (fast-math, etc.)
            constantFoldingEnabled_ = true;
            deadCodeEnabled_ = true;
            inliningEnabled_ = true;
            tailCallEnabled_ = true;
            ctfeEnabled_ = true;
            ssaEnabled_ = true;
            loopOptEnabled_ = true;
            schedulingEnabled_ = true;
            simplifyCFGEnabled_ = true;
            reassociateEnabled_ = true;
            sroaEnabled_ = true;
            mem2regEnabled_ = true;
            pgoEnabled_ = true;  // Enable PGO at Ofast
            aggressiveInlining_ = true;
            maxInlineStatements_ = 100;
            maxInlineCallCount_ = 50;
            break;
    }
}

void Optimizer::optimize(Program& ast) {
    totalTransformations_ = 0;
    
    // PHASE 0: Profile-Guided Optimization (if profile data available)
    if (pgoEnabled_ && !profileFile_.empty()) {
        auto pgo = createPGOPass(profileFile_);
        pgo->run(ast);
        totalTransformations_ += pgo->transformations();
        if (verbose_ && pgo->transformations() > 0) {
            std::cout << "[Optimizer] ProfileGuidedOptimization: " 
                      << pgo->transformations() << " transformation(s)\n";
        }
    }
    
    // PHASE 1: Pre-loop optimizations
    // Algebraic simplification first (cleans up expressions)
    if (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) {
        auto algebraic = std::make_unique<AlgebraicSimplificationPass>();
        algebraic->run(ast);
        totalTransformations_ += algebraic->transformations();
        if (verbose_ && algebraic->transformations() > 0) {
            std::cout << "[Optimizer] AlgebraicSimplification: " 
                      << algebraic->transformations() << " transformation(s)\n";
        }
    }
    
    // Reassociate expressions to expose more constant folding opportunities
    // Example: (a + 1) + 2 → a + 3
    if (reassociateEnabled_ && optLevel_ >= OptLevel::O2) {
        auto reassoc = std::make_unique<ReassociatePass>();
        reassoc->run(ast);
        totalTransformations_ += reassoc->transformations();
        if (verbose_ && reassoc->transformations() > 0) {
            std::cout << "[Optimizer] Reassociate: " 
                      << reassoc->transformations() << " transformation(s)\n";
        }
    }
    
    // Constant folding to simplify loop bounds
    if (constantFoldingEnabled_) {
        auto cf = std::make_unique<ConstantFoldingPass>();
        cf->run(ast);
        totalTransformations_ += cf->transformations();
        if (verbose_ && cf->transformations() > 0) {
            std::cout << "[Optimizer] ConstantFolding (phase 1): " 
                      << cf->transformations() << " transformation(s)\n";
        }
    }
    
    // SROA - Scalar Replacement of Aggregates
    // Break up records/structs into individual scalar variables
    if (sroaEnabled_ && optLevel_ >= OptLevel::O2) {
        auto sroa = std::make_unique<SROAPass>();
        sroa->run(ast);
        totalTransformations_ += sroa->transformations();
        if (verbose_ && sroa->transformations() > 0) {
            std::cout << "[Optimizer] SROA: " 
                      << sroa->transformations() << " transformation(s)\n";
        }
    }
    
    // mem2reg - Memory to Register Promotion
    // Promote stack allocations to SSA registers
    if (mem2regEnabled_ && optLevel_ >= OptLevel::O2) {
        auto mem2reg = std::make_unique<Mem2RegPass>();
        mem2reg->run(ast);
        totalTransformations_ += mem2reg->transformations();
        if (verbose_ && mem2reg->transformations() > 0) {
            std::cout << "[Optimizer] Mem2Reg: " 
                      << mem2reg->transformations() << " transformation(s)\n";
        }
    }
    
    // CTFE to evaluate pure functions
    if (ctfeEnabled_) {
        auto ctfe = std::make_unique<CTFEPass>();
        ctfe->run(ast);
        totalTransformations_ += ctfe->transformations();
        if (verbose_ && ctfe->transformations() > 0) {
            std::cout << "[Optimizer] CTFE: " << ctfe->transformations() << " transformation(s)\n";
        }
    }
    
    // PHASE 2: Loop optimizations
    if (loopOptEnabled_) {
        // Loop Simplify - canonicalize loop structure (preheader, single latch, dedicated exits)
        // This should run first to prepare loops for other optimizations
        if (optLevel_ >= OptLevel::O2) {
            auto loopSimplify = std::make_unique<LoopSimplifyPass>();
            loopSimplify->run(ast);
            totalTransformations_ += loopSimplify->transformations();
            if (verbose_ && loopSimplify->transformations() > 0) {
                std::cout << "[Optimizer] LoopSimplify: " 
                          << loopSimplify->transformations() << " transformation(s)\n";
            }
        }
        
        // Loop Rotation - transform loops to have exit at bottom
        // This enables better LICM and loop unrolling
        if (optLevel_ >= OptLevel::O2) {
            auto loopRotate = std::make_unique<LoopRotationPass>();
            loopRotate->run(ast);
            totalTransformations_ += loopRotate->transformations();
            if (verbose_ && loopRotate->transformations() > 0) {
                std::cout << "[Optimizer] LoopRotation: " 
                          << loopRotate->transformations() << " transformation(s)\n";
            }
        }
        
        // Induction Variable Simplification - canonicalize IVs, compute trip counts
        if (optLevel_ >= OptLevel::O2) {
            auto indvar = std::make_unique<IndVarSimplifyPass>();
            indvar->run(ast);
            totalTransformations_ += indvar->transformations();
            if (verbose_ && indvar->transformations() > 0) {
                std::cout << "[Optimizer] IndVarSimplify: " 
                          << indvar->transformations() << " transformation(s)\n";
            }
        }
        
        auto loopOpt = std::make_unique<LoopOptimizationPass>();
        loopOpt->run(ast);
        totalTransformations_ += loopOpt->transformations();
        if (verbose_ && loopOpt->transformations() > 0) {
            std::cout << "[Optimizer] LoopOptimization: " 
                      << loopOpt->transformations() << " transformation(s)\n";
        }
        
        // Enhanced LICM for more aggressive hoisting (O3/Ofast)
        if (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) {
            auto enhancedLicm = std::make_unique<EnhancedLICMPass>();
            enhancedLicm->run(ast);
            totalTransformations_ += enhancedLicm->transformations();
            if (verbose_ && enhancedLicm->transformations() > 0) {
                std::cout << "[Optimizer] EnhancedLICM: " 
                          << enhancedLicm->transformations() << " transformation(s)\n";
            }
        }
        
        // Loop Deletion - remove loops with no side effects and unused results
        if (optLevel_ >= OptLevel::O2) {
            auto loopDel = std::make_unique<LoopDeletionPass>();
            loopDel->run(ast);
            totalTransformations_ += loopDel->transformations();
            if (verbose_ && loopDel->transformations() > 0) {
                std::cout << "[Optimizer] LoopDeletion: " 
                          << loopDel->transformations() << " transformation(s)\n";
            }
        }
        
        // Loop Idiom Recognition - convert loops to memset/memcpy
        if (optLevel_ >= OptLevel::O2) {
            auto loopIdiom = std::make_unique<LoopIdiomRecognitionPass>();
            loopIdiom->run(ast);
            totalTransformations_ += loopIdiom->transformations();
            if (verbose_ && loopIdiom->transformations() > 0) {
                std::cout << "[Optimizer] LoopIdiomRecognition: " 
                          << loopIdiom->transformations() << " transformation(s)\n";
            }
        }
    }
    
    // PHASE 3: Post-loop constant folding (CRITICAL - folds unrolled loop expressions)
    // Run a limited number of iterations
    int maxIterations = (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) ? 5 : 3;
    
    for (int iteration = 1; iteration <= maxIterations; ++iteration) {
        int iterTransformations = 0;
        
        // Constant folding on unrolled code
        if (constantFoldingEnabled_) {
            auto cf = std::make_unique<ConstantFoldingPass>();
            cf->run(ast);
            iterTransformations += cf->transformations();
            totalTransformations_ += cf->transformations();
            if (verbose_ && cf->transformations() > 0) {
                std::cout << "[Optimizer] ConstantFolding (iter " << iteration << "): " 
                          << cf->transformations() << " transformation(s)\n";
            }
        }
        
        // Constant propagation to track values through assignments
        if (constantFoldingEnabled_) {
            auto cp = std::make_unique<ConstantPropagationPass>();
            cp->run(ast);
            iterTransformations += cp->transformations();
            totalTransformations_ += cp->transformations();
            if (verbose_ && cp->transformations() > 0) {
                std::cout << "[Optimizer] ConstantPropagation (iter " << iteration << "): " 
                          << cp->transformations() << " transformation(s)\n";
            }
        }
        
        // Dead code elimination
        if (deadCodeEnabled_) {
            auto dce = std::make_unique<DeadCodeEliminationPass>();
            dce->run(ast);
            iterTransformations += dce->transformations();
            totalTransformations_ += dce->transformations();
            if (verbose_ && dce->transformations() > 0) {
                std::cout << "[Optimizer] DeadCodeElimination (iter " << iteration << "): " 
                          << dce->transformations() << " transformation(s)\n";
            }
        }
        
        // Stop early if no transformations in this iteration
        if (iterTransformations == 0) {
            break;
        }
    }
    
    // PHASE 4: Function-level optimizations
    if (inliningEnabled_) {
        auto inliningPass = std::make_unique<InliningPass>();
        inliningPass->setMaxInlineStatements(maxInlineStatements_);
        inliningPass->setMaxInlineCallCount(maxInlineCallCount_);
        inliningPass->setAggressiveInlining(aggressiveInlining_);
        inliningPass->run(ast);
        totalTransformations_ += inliningPass->transformations();
        if (verbose_ && inliningPass->transformations() > 0) {
            std::cout << "[Optimizer] Inlining: " 
                      << inliningPass->transformations() << " transformation(s)\n";
        }
    }
    
    if (tailCallEnabled_) {
        auto tco = std::make_unique<TailCallOptimizationPass>();
        tco->run(ast);
        totalTransformations_ += tco->transformations();
        if (verbose_ && tco->transformations() > 0) {
            std::cout << "[Optimizer] TailCallOptimization: " 
                      << tco->transformations() << " transformation(s)\n";
        }
    }
    
    // PHASE 4.5: Inter-Procedural Optimizations (IPO)
    // IPSCCP - Inter-Procedural Sparse Conditional Constant Propagation
    if (optLevel_ >= OptLevel::O2) {
        auto ipsccp = std::make_unique<IPSCCPPass>();
        ipsccp->run(ast);
        totalTransformations_ += ipsccp->transformations();
        if (verbose_ && ipsccp->transformations() > 0) {
            std::cout << "[Optimizer] IPSCCP: " 
                      << ipsccp->transformations() << " transformation(s)\n";
        }
    }
    
    // Dead Argument Elimination - remove unused function arguments
    if (optLevel_ >= OptLevel::O2) {
        auto deadArgElim = std::make_unique<DeadArgElimPass>();
        deadArgElim->run(ast);
        totalTransformations_ += deadArgElim->transformations();
        if (verbose_ && deadArgElim->transformations() > 0) {
            std::cout << "[Optimizer] DeadArgElim: " 
                      << deadArgElim->transformations() << " transformation(s)\n";
        }
    }
    
    // GlobalOpt - Optimize global variables (constify, eliminate unused, etc.)
    if (optLevel_ >= OptLevel::O2) {
        auto globalOpt = std::make_unique<GlobalOptPass>();
        globalOpt->run(ast);
        totalTransformations_ += globalOpt->transformations();
        if (verbose_ && globalOpt->transformations() > 0) {
            std::cout << "[Optimizer] GlobalOpt: " 
                      << globalOpt->transformations() << " transformation(s)\n";
        }
    }
    
    // Speculative Devirtualization - convert virtual calls to direct calls
    if (optLevel_ >= OptLevel::O2) {
        auto specDevirt = std::make_unique<SpeculativeDevirtPass>();
        specDevirt->run(ast);
        totalTransformations_ += specDevirt->transformations();
        if (verbose_ && specDevirt->transformations() > 0) {
            std::cout << "[Optimizer] SpeculativeDevirt: " 
                      << specDevirt->transformations() << " transformation(s)\n";
        }
    }
    
    // Partial Inlining - inline hot paths, keep cold paths as calls (O3/Ofast)
    if (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) {
        auto partialInline = std::make_unique<PartialInliningPass>();
        partialInline->run(ast);
        totalTransformations_ += partialInline->transformations();
        if (verbose_ && partialInline->transformations() > 0) {
            std::cout << "[Optimizer] PartialInlining: " 
                      << partialInline->transformations() << " transformation(s)\n";
        }
    }
    
    // PHASE 5: Advanced optimizations (O3/Ofast only)
    if (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) {
        // GVN-PRE for load/store optimization and partial redundancy elimination
        auto gvnPre = std::make_unique<GVNPREPass>();
        gvnPre->run(ast);
        totalTransformations_ += gvnPre->transformations();
        if (verbose_ && gvnPre->transformations() > 0) {
            std::cout << "[Optimizer] GVN-PRE: " 
                      << gvnPre->transformations() << " transformation(s)\n";
        }
        
        // Advanced strength reduction for more complex patterns
        auto advStrength = std::make_unique<AdvancedStrengthReductionPass>();
        advStrength->run(ast);
        totalTransformations_ += advStrength->transformations();
        if (verbose_ && advStrength->transformations() > 0) {
            std::cout << "[Optimizer] AdvancedStrengthReduction: " 
                      << advStrength->transformations() << " transformation(s)\n";
        }
        
        // Run another round of constant folding after strength reduction
        auto cf = std::make_unique<ConstantFoldingPass>();
        cf->run(ast);
        totalTransformations_ += cf->transformations();
        if (verbose_ && cf->transformations() > 0) {
            std::cout << "[Optimizer] ConstantFolding (post-strength): " 
                      << cf->transformations() << " transformation(s)\n";
        }
    }
    
    // PHASE 6: CFG Simplification (cleanup control flow)
    // Jump Threading - thread jumps through predictable conditions
    if (simplifyCFGEnabled_ && optLevel_ >= OptLevel::O2) {
        auto jumpThread = std::make_unique<JumpThreadingPass>();
        jumpThread->run(ast);
        totalTransformations_ += jumpThread->transformations();
        if (verbose_ && jumpThread->transformations() > 0) {
            std::cout << "[Optimizer] JumpThreading: " 
                      << jumpThread->transformations() << " transformation(s)\n";
        }
    }
    
    // Correlated Value Propagation - use range analysis to simplify comparisons
    if (optLevel_ >= OptLevel::O2) {
        auto cvp = std::make_unique<CorrelatedValuePropagationPass>();
        cvp->run(ast);
        totalTransformations_ += cvp->transformations();
        if (verbose_ && cvp->transformations() > 0) {
            std::cout << "[Optimizer] CorrelatedValuePropagation: " 
                      << cvp->transformations() << " transformation(s)\n";
        }
    }
    
    // Constraint Elimination - use constraint solving to eliminate redundant checks
    if (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) {
        auto constraintElim = std::make_unique<ConstraintEliminationPass>();
        constraintElim->run(ast);
        totalTransformations_ += constraintElim->transformations();
        if (verbose_ && constraintElim->transformations() > 0) {
            std::cout << "[Optimizer] ConstraintElimination: " 
                      << constraintElim->transformations() << " transformation(s)\n";
        }
    }
    
    if (simplifyCFGEnabled_) {
        auto cfg = std::make_unique<SimplifyCFGPass>();
        cfg->run(ast);
        totalTransformations_ += cfg->transformations();
        if (verbose_ && cfg->transformations() > 0) {
            std::cout << "[Optimizer] SimplifyCFG: " 
                      << cfg->transformations() << " transformation(s)\n";
        }
    }
    
    // PHASE 7: Final cleanup pass
    if (constantFoldingEnabled_) {
        auto cf = std::make_unique<ConstantFoldingPass>();
        cf->run(ast);
        totalTransformations_ += cf->transformations();
        if (verbose_ && cf->transformations() > 0) {
            std::cout << "[Optimizer] ConstantFolding (final): " 
                      << cf->transformations() << " transformation(s)\n";
        }
    }
    
    // Dead Store Elimination - remove redundant stores
    if (deadCodeEnabled_) {
        auto dse = std::make_unique<DeadStoreEliminationPass>();
        dse->run(ast);
        totalTransformations_ += dse->transformations();
        if (verbose_ && dse->transformations() > 0) {
            std::cout << "[Optimizer] DeadStoreElimination: " 
                      << dse->transformations() << " transformation(s)\n";
        }
    }
    
    // MemCpyOpt - merge adjacent stores into memset/memcpy
    if (optLevel_ >= OptLevel::O2) {
        auto memcpyopt = std::make_unique<MemCpyOptPass>();
        memcpyopt->run(ast);
        totalTransformations_ += memcpyopt->transformations();
        if (verbose_ && memcpyopt->transformations() > 0) {
            std::cout << "[Optimizer] MemCpyOpt: " 
                      << memcpyopt->transformations() << " transformation(s)\n";
        }
    }

    if (deadCodeEnabled_) {
        auto dce = std::make_unique<DeadCodeEliminationPass>();
        dce->run(ast);
        totalTransformations_ += dce->transformations();
        if (verbose_ && dce->transformations() > 0) {
            std::cout << "[Optimizer] DeadCodeElimination (final): " 
                      << dce->transformations() << " transformation(s)\n";
        }
        
        // ADCE for more aggressive dead code elimination (O3/Ofast)
        if (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) {
            auto adce = std::make_unique<ADCEPass>();
            adce->run(ast);
            totalTransformations_ += adce->transformations();
            if (verbose_ && adce->transformations() > 0) {
                std::cout << "[Optimizer] ADCE: " 
                          << adce->transformations() << " transformation(s)\n";
            }
            
            // BDCE - Bit-Tracking Dead Code Elimination
            auto bdce = std::make_unique<BDCEPass>();
            bdce->run(ast);
            totalTransformations_ += bdce->transformations();
            if (verbose_ && bdce->transformations() > 0) {
                std::cout << "[Optimizer] BDCE: " 
                          << bdce->transformations() << " transformation(s)\n";
            }
        }
    }
    
    // PHASE 8: Instruction Scheduling (O3/Ofast only)
    if (schedulingEnabled_ && (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast)) {
        auto scheduler = std::make_unique<InstructionSchedulerPass>();
        scheduler->run(ast);
        totalTransformations_ += scheduler->transformations();
        if (verbose_ && scheduler->transformations() > 0) {
            std::cout << "[Optimizer] InstructionScheduler: " 
                      << scheduler->transformations() << " transformation(s)\n";
        }
    }
    
    if (verbose_) {
        std::cout << "[Optimizer] Total: " << totalTransformations_ << " transformation(s)\n";
    }
}

void Optimizer::enableConstantFolding(bool enable) {
    constantFoldingEnabled_ = enable;
}

void Optimizer::enableDeadCodeElimination(bool enable) {
    deadCodeEnabled_ = enable;
}

void Optimizer::enableDeadStoreElimination(bool enable) {
    deadStoreEnabled_ = enable;
}

void Optimizer::enableInlining(bool enable) {
    inliningEnabled_ = enable;
}

void Optimizer::enableTailCallOptimization(bool enable) {
    tailCallEnabled_ = enable;
}

void Optimizer::enableCTFE(bool enable) {
    ctfeEnabled_ = enable;
}

void Optimizer::enableSimplifyCFG(bool enable) {
    simplifyCFGEnabled_ = enable;
}

void Optimizer::enableSSA(bool enable) {
    ssaEnabled_ = enable;
}

void Optimizer::enableLoopOptimization(bool enable) {
    loopOptEnabled_ = enable;
}

void Optimizer::enableInstructionScheduling(bool enable) {
    schedulingEnabled_ = enable;
}

void Optimizer::enablePGO(bool enable) {
    pgoEnabled_ = enable;
}

void Optimizer::enableReassociate(bool enable) {
    reassociateEnabled_ = enable;
}

void Optimizer::enableSROA(bool enable) {
    sroaEnabled_ = enable;
}

void Optimizer::enableMem2Reg(bool enable) {
    mem2regEnabled_ = enable;
}

std::unique_ptr<SSAModule> Optimizer::buildSSA(Program& ast) {
    SSABuilder builder;
    return builder.build(ast);
}

void Optimizer::optimizeSSA(SSAModule& module) {
    SSAOptimizer optimizer;
    optimizer.optimize(module);
}

// SSA Optimization Pass implementation
void SSAOptimizationPass::run(Program& ast) {
    transformations_ = 0;
    
    // Build SSA form
    SSABuilder builder;
    auto module = builder.build(ast);
    
    // Run SSA optimizations
    SSAOptimizer optimizer;
    optimizer.optimize(*module);
    
    // Note: In a full implementation, we would convert back to AST
    // For now, SSA optimizations are informational
    transformations_ = 1;  // Mark that we ran
}

std::unique_ptr<Optimizer> createDefaultOptimizer() {
    return std::make_unique<Optimizer>();
}

std::unique_ptr<Optimizer> createOptimizer(OptLevel level) {
    auto opt = std::make_unique<Optimizer>();
    opt->setOptLevel(level);
    return opt;
}

} // namespace tyl
