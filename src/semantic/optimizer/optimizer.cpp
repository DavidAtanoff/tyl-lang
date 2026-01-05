// Tyl Compiler - Optimizer Implementation
// Complete Tier 2-5 Optimization Pipeline
#include "optimizer.h"
#include "constant_folding.h"
#include "constant_propagation.h"
#include "dead_code.h"
#include "inlining.h"
#include "tail_call.h"
#include "ctfe.h"
#include "ssa.h"
#include "loop_optimizer.h"
#include "instruction_scheduler.h"
#include "cse.h"
#include "gvn.h"
#include "algebraic.h"
#include "pgo.h"
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
            break;
            
        case OptLevel::O1:
            // Basic optimizations - constant folding, DCE
            constantFoldingEnabled_ = true;
            deadCodeEnabled_ = true;
            inliningEnabled_ = false;
            tailCallEnabled_ = false;
            ctfeEnabled_ = false;
            ssaEnabled_ = false;
            loopOptEnabled_ = false;
            schedulingEnabled_ = false;
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
        auto loopOpt = std::make_unique<LoopOptimizationPass>();
        loopOpt->run(ast);
        totalTransformations_ += loopOpt->transformations();
        if (verbose_ && loopOpt->transformations() > 0) {
            std::cout << "[Optimizer] LoopOptimization: " 
                      << loopOpt->transformations() << " transformation(s)\n";
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
    
    // PHASE 5: Advanced optimizations (O3/Ofast only)
    if (optLevel_ >= OptLevel::O3 || optLevel_ == OptLevel::Ofast) {
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
    
    // PHASE 6: Final cleanup pass
    if (constantFoldingEnabled_) {
        auto cf = std::make_unique<ConstantFoldingPass>();
        cf->run(ast);
        totalTransformations_ += cf->transformations();
        if (verbose_ && cf->transformations() > 0) {
            std::cout << "[Optimizer] ConstantFolding (final): " 
                      << cf->transformations() << " transformation(s)\n";
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

void Optimizer::enableInlining(bool enable) {
    inliningEnabled_ = enable;
}

void Optimizer::enableTailCallOptimization(bool enable) {
    tailCallEnabled_ = enable;
}

void Optimizer::enableCTFE(bool enable) {
    ctfeEnabled_ = enable;
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
