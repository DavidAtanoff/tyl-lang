// Tyl Compiler - Peephole Optimizer for x64 Code
// Performs local optimizations on generated machine code
// Enhanced with register coalescing and more patterns for O3 optimization
#ifndef TYL_PEEPHOLE_H
#define TYL_PEEPHOLE_H

#include <vector>
#include <cstdint>

namespace tyl {

// Peephole optimizer that works on raw x64 machine code
class PeepholeOptimizer {
public:
    // Optimize the code buffer in place
    // Returns the new size (may be smaller due to removed instructions)
    size_t optimize(std::vector<uint8_t>& code);
    
    // Statistics
    int removedBytes() const { return removedBytes_; }
    int optimizationCount() const { return optimizationCount_; }
    
    // Configuration
    void setAggressiveMode(bool aggressive) { aggressiveMode_ = aggressive; }
    
private:
    int removedBytes_ = 0;
    int optimizationCount_ = 0;
    bool aggressiveMode_ = true;  // Enable aggressive optimizations by default
    
    // Pattern matchers and replacers
    bool optimizePushPop(std::vector<uint8_t>& code, size_t& i);
    bool optimizeMovMov(std::vector<uint8_t>& code, size_t& i);
    bool optimizeSmallConstants(std::vector<uint8_t>& code, size_t& i);
    bool optimizeRedundantMov(std::vector<uint8_t>& code, size_t& i);
    bool optimizeDirectPushPop(std::vector<uint8_t>& code, size_t& i);
    
    // New optimization patterns for O3
    bool optimizeRegisterCoalescing(std::vector<uint8_t>& code, size_t& i);
    bool optimizeXorZero(std::vector<uint8_t>& code, size_t& i);
    bool optimizeRedundantXor(std::vector<uint8_t>& code, size_t& i);
    bool optimizeIncDec(std::vector<uint8_t>& code, size_t& i);
    bool optimizeLeaArithmetic(std::vector<uint8_t>& code, size_t& i);
    bool optimizeTestCmp(std::vector<uint8_t>& code, size_t& i);
    bool optimizeXorBeforeMovImm(std::vector<uint8_t>& code, size_t& i);
    bool optimizeMovZeroExtend(std::vector<uint8_t>& code, size_t& i);
    bool optimizeRedundantPushPop(std::vector<uint8_t>& code, size_t& i);
    
    // Remove NOP instructions (final cleanup pass)
    void removeNops(std::vector<uint8_t>& code);
    
    // Helper to check instruction patterns
    bool isPushRax(const std::vector<uint8_t>& code, size_t i);
    bool isPushRcx(const std::vector<uint8_t>& code, size_t i);
    bool isPopRcx(const std::vector<uint8_t>& code, size_t i);
    bool isPopRdx(const std::vector<uint8_t>& code, size_t i);
    bool isPopRax(const std::vector<uint8_t>& code, size_t i);
    bool isMovRaxImm64(const std::vector<uint8_t>& code, size_t i);
    bool isMovRaxMemRbp(const std::vector<uint8_t>& code, size_t i);
    bool isNop(const std::vector<uint8_t>& code, size_t i);
    
    // New helper functions
    bool isXorRaxRax(const std::vector<uint8_t>& code, size_t i);
    bool isXorRcxRcx(const std::vector<uint8_t>& code, size_t i);
    bool isMovRaxRcx(const std::vector<uint8_t>& code, size_t i);
    bool isMovRcxRax(const std::vector<uint8_t>& code, size_t i);
    bool isMovR12Rax(const std::vector<uint8_t>& code, size_t i);
    bool isMovR13Rax(const std::vector<uint8_t>& code, size_t i);
    bool isAddRaxImm(const std::vector<uint8_t>& code, size_t i, int32_t& imm);
    bool isSubRaxImm(const std::vector<uint8_t>& code, size_t i, int32_t& imm);
    
    // Get immediate value from mov rax, imm64
    int64_t getImm64(const std::vector<uint8_t>& code, size_t i);
    
    // Remove bytes from code
    void removeBytes(std::vector<uint8_t>& code, size_t start, size_t count);
    
    // Replace bytes in code
    void replaceBytes(std::vector<uint8_t>& code, size_t start, 
                      const std::vector<uint8_t>& replacement, size_t oldLen);
    
    // NOP out a range of bytes
    void nopOut(std::vector<uint8_t>& code, size_t start, size_t count);
};

} // namespace tyl

#endif // TYL_PEEPHOLE_H
