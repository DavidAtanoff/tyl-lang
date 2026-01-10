// Tyl Compiler - Optimizer Infrastructure
// Tier 2-5 Optimization Passes: Constant Folding, DCE, Inlining, TCO, SSA, Loop Opts, Scheduling, CFG
#ifndef TYL_OPTIMIZER_H
#define TYL_OPTIMIZER_H

#include "frontend/ast/ast.h"
#include <memory>
#include <vector>
#include <string>

namespace tyl {

// Forward declarations
struct SSAModule;
class LoopOptimizationPass;
class InstructionSchedulerPass;
class CSEPass;
class GVNPass;
class CopyPropagationPass;
class AlgebraicSimplificationPass;
class AdvancedStrengthReductionPass;
class PGOPass;
class SimplifyCFGPass;
struct ProgramProfile;

// Enhanced optimization passes
class ADCEPass;
class EnhancedDCEPass;
class GVNPREPass;
class LoadEliminationPass;
class StoreSinkingPass;
class EnhancedLICMPass;
class InvariantExpressionHoistingPass;

// New optimization passes (January 2026)
class ReassociatePass;
class SROAPass;
class Mem2RegPass;

// Loop and CFG optimization passes (January 2026)
class JumpThreadingPass;
class LoopRotationPass;
class IndVarSimplifyPass;

// New loop optimization passes (January 2026 - Phase 4)
class LoopUnswitchPass;
class LoopPeelingPass;

// New optimization passes (January 2026 - Phase 2)
class LoopDeletionPass;
class LoopIdiomRecognitionPass;
class MemCpyOptPass;

// New optimization passes (January 2026 - Phase 3)
class BDCEPass;
class CorrelatedValuePropagationPass;
class ConstraintEliminationPass;

// Loop canonicalization passes
class LoopSimplifyPass;

// IPO (Inter-Procedural Optimization) passes
class IPSCCPPass;
class DeadArgElimPass;
class GlobalOptPass;
class PartialInliningPass;
class SpeculativeDevirtPass;

// Base class for all optimization passes
class OptimizationPass {
public:
    virtual ~OptimizationPass() = default;
    virtual void run(Program& ast) = 0;
    virtual std::string name() const = 0;
    
    // Statistics tracking
    int transformations() const { return transformations_; }
    void resetStats() { transformations_ = 0; }
    
protected:
    int transformations_ = 0;
};

// Optimization level presets (LLVM/Clang compatible)
enum class OptLevel {
    O0,    // No optimization - fastest compile, debug friendly
    O1,    // Basic optimizations - reduces code size, improves perf without slow compile
    O2,    // Moderate optimization - typical production level
    O3,    // Aggressive optimization - includes vectorization, loop unrolling, more inlining
    Os,    // Optimize for size - like O2 but prefers smaller code
    Oz,    // Aggressive size optimization - even smaller than Os
    Ofast  // Like O3 plus unsafe optimizations (fast-math, etc.)
};

// Main optimizer that orchestrates passes
class Optimizer {
public:
    Optimizer();
    
    // Add a pass to the pipeline
    void addPass(std::unique_ptr<OptimizationPass> pass);
    
    // Run all passes on the AST
    void optimize(Program& ast);
    
    // Configuration
    void setVerbose(bool v) { verbose_ = v; }
    bool verbose() const { return verbose_; }
    void setOptLevel(OptLevel level);
    OptLevel optLevel() const { return optLevel_; }
    
    // Enable/disable specific passes
    void enableConstantFolding(bool enable = true);
    void enableDeadCodeElimination(bool enable = true);
    void enableDeadStoreElimination(bool enable = true);
    void enableInlining(bool enable = true);
    void enableTailCallOptimization(bool enable = true);
    void enableCTFE(bool enable = true);
    void enableSSA(bool enable = true);
    void enableLoopOptimization(bool enable = true);
    void enableInstructionScheduling(bool enable = true);
    void enablePGO(bool enable = true);
    void enableSimplifyCFG(bool enable = true);
    void enableReassociate(bool enable = true);
    void enableSROA(bool enable = true);
    void enableMem2Reg(bool enable = true);
    
    // PGO configuration
    void setProfileFile(const std::string& filename) { profileFile_ = filename; }
    const std::string& profileFile() const { return profileFile_; }
    
    // SSA-specific methods
    bool useSSA() const { return ssaEnabled_; }
    std::unique_ptr<SSAModule> buildSSA(Program& ast);
    void optimizeSSA(SSAModule& module);
    
    // Inlining configuration
    void setMaxInlineStatements(size_t max) { maxInlineStatements_ = max; }
    void setMaxInlineCallCount(size_t max) { maxInlineCallCount_ = max; }
    void setAggressiveInlining(bool aggressive) { aggressiveInlining_ = aggressive; }
    
    // Get statistics
    int totalTransformations() const { return totalTransformations_; }
    
private:
    std::vector<std::unique_ptr<OptimizationPass>> passes_;
    bool verbose_ = false;
    int totalTransformations_ = 0;
    OptLevel optLevel_ = OptLevel::O2;
    
    // Pass enable flags
    bool constantFoldingEnabled_ = true;
    bool deadCodeEnabled_ = true;
    bool deadStoreEnabled_ = true;
    bool inliningEnabled_ = true;
    bool tailCallEnabled_ = true;
    bool ctfeEnabled_ = true;
    bool ssaEnabled_ = false;
    bool loopOptEnabled_ = true;
    bool schedulingEnabled_ = true;
    bool pgoEnabled_ = false;
    bool simplifyCFGEnabled_ = true;  // CFG simplification
    bool reassociateEnabled_ = true;  // Expression reassociation
    bool sroaEnabled_ = true;         // Scalar replacement of aggregates
    bool mem2regEnabled_ = true;      // Memory to register promotion
    std::string profileFile_;
    
    // Inlining configuration
    size_t maxInlineStatements_ = 10;
    size_t maxInlineCallCount_ = 5;
    bool aggressiveInlining_ = false;
};

// SSA Optimization Pass - runs SSA-based optimizations
class SSAOptimizationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "SSAOptimization"; }
};

// Factory function to create default optimizer with all Tier 2 passes
std::unique_ptr<Optimizer> createDefaultOptimizer();

// Factory function to create optimizer with specific optimization level
std::unique_ptr<Optimizer> createOptimizer(OptLevel level);

// Enable/disable CTFE
void enableCTFE(bool enable = true);

} // namespace tyl

#endif // TYL_OPTIMIZER_H
