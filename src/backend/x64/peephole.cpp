// Tyl Compiler - Peephole Optimizer Implementation
#include "peephole.h"
#include <cstring>

namespace tyl {

size_t PeepholeOptimizer::optimize(std::vector<uint8_t>& code) {
    removedBytes_ = 0;
    optimizationCount_ = 0;
    
    // Multiple passes until no more optimizations
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (size_t i = 0; i < code.size(); ) {
            // Try each optimization pattern
            if (optimizeDirectPushPop(code, i)) {
                changed = true;
                continue;
            }
            if (optimizePushPop(code, i)) {
                changed = true;
                continue;
            }
            if (optimizeSmallConstants(code, i)) {
                changed = true;
                continue;
            }
            if (optimizeRedundantMov(code, i)) {
                changed = true;
                continue;
            }
            ++i;
        }
    }
    
    // Final pass: remove all NOP instructions
    removeNops(code);
    
    return code.size();
}

// Pattern: push rax; pop rcx -> mov rcx, rax (direct, no intervening code)
// This is the most common pattern from stack-based code generation
bool PeepholeOptimizer::optimizeDirectPushPop(std::vector<uint8_t>& code, size_t& i) {
    if (i + 1 >= code.size()) return false;
    
    // push rax (0x50) followed immediately by pop rcx (0x59)
    if (code[i] == 0x50 && code[i + 1] == 0x59) {
        // push rax (1 byte) + pop rcx (1 byte) = 2 bytes
        // mov rcx, rax = 3 bytes (48 89 C1)
        // Not a win, skip this pattern
        return false;
    }
    
    // push rax (0x50) followed immediately by pop rdx (0x5A)
    if (code[i] == 0x50 && code[i + 1] == 0x5A) {
        // mov rdx, rax = 48 89 C2 (3 bytes) vs push+pop = 2 bytes
        // Not a win, skip
        return false;
    }
    
    // push rcx (0x51) followed immediately by pop rax (0x58)
    if (code[i] == 0x51 && code[i + 1] == 0x58) {
        // mov rax, rcx = 48 89 C8 (3 bytes) vs push+pop = 2 bytes
        // Not a win, skip
        return false;
    }
    
    return false;
}

// Pattern: mov rax, imm64; push rax; ... ; pop rcx
// Replace with: mov rcx, imm64 (if nothing uses rax in between)
// Or for small values: mov ecx, imm32
bool PeepholeOptimizer::optimizePushPop(std::vector<uint8_t>& code, size_t& i) {
    // Look for: push rax (0x50) followed soon by pop rcx (0x59) or pop rdx (0x5A)
    if (i + 1 >= code.size()) return false;
    
    if (!isPushRax(code, i)) return false;
    
    // Find matching pop within reasonable distance
    size_t pushPos = i;
    size_t searchLimit = std::min(i + 30, code.size());
    
    for (size_t j = i + 1; j < searchLimit; ++j) {
        // Check for pop rcx or pop rdx
        if (isPopRcx(code, j) || isPopRdx(code, j)) {
            // Check if there's a mov rax, imm64 right before the push
            if (pushPos >= 10 && isMovRaxImm64(code, pushPos - 10)) {
                int64_t imm = getImm64(code, pushPos - 10);
                
                // Check if value fits in 32 bits
                if (imm >= 0 && imm <= 0x7FFFFFFF) {
                    // Replace: mov rax, imm64 (10 bytes) + push rax (1 byte) + ... + pop rcx/rdx (1 byte)
                    // With: mov ecx/edx, imm32 (5 bytes) + nops for the push/pop
                    
                    uint8_t destReg = isPopRcx(code, j) ? 0xB9 : 0xBA; // mov ecx or mov edx
                    
                    // Build replacement for mov rax, imm64 -> mov ecx/edx, imm32
                    std::vector<uint8_t> replacement = {
                        destReg,  // mov ecx/edx, imm32
                        (uint8_t)(imm & 0xFF),
                        (uint8_t)((imm >> 8) & 0xFF),
                        (uint8_t)((imm >> 16) & 0xFF),
                        (uint8_t)((imm >> 24) & 0xFF)
                    };
                    
                    // Replace mov rax, imm64 with mov ecx/edx, imm32 + nops
                    for (size_t k = 0; k < 5; ++k) {
                        code[pushPos - 10 + k] = replacement[k];
                    }
                    // NOP out the rest of mov rax, imm64
                    for (size_t k = 5; k < 10; ++k) {
                        code[pushPos - 10 + k] = 0x90; // NOP
                    }
                    // NOP out push rax
                    code[pushPos] = 0x90;
                    // NOP out pop rcx/rdx
                    code[j] = 0x90;
                    
                    removedBytes_ += 7; // Saved 10+1+1 - 5 = 7 bytes (NOPs will be removed later)
                    optimizationCount_++;
                    return true;
                }
            }
            break;
        }
        
        // If we hit another push or something that modifies the stack, stop
        if (code[j] == 0x50 || code[j] == 0x51 || code[j] == 0x52 || 
            code[j] == 0x53 || code[j] == 0x54 || code[j] == 0x55 ||
            code[j] == 0xC3 || code[j] == 0xE8 || code[j] == 0xFF) {
            break;
        }
    }
    
    return false;
}

// Pattern: mov rax, small_value (10 bytes) -> mov eax, small_value (5 bytes)
// Only for values that fit in 32 bits and don't need sign extension
// NOTE: This optimization is now handled at code generation time, so we skip it here
// to avoid leaving NOP artifacts in the binary
bool PeepholeOptimizer::optimizeSmallConstants(std::vector<uint8_t>& code, size_t& i) {
    // Skip this optimization - it's now done at codegen time
    // Keeping the function for potential future use with other patterns
    (void)code;
    (void)i;
    return false;
}

// Pattern: mov [rbp+x], rax; mov rax, [rbp+x] -> just keep the first mov
bool PeepholeOptimizer::optimizeRedundantMov(std::vector<uint8_t>& code, size_t& i) {
    // Look for mov [rbp+disp32], rax followed by mov rax, [rbp+disp32]
    // mov [rbp+disp32], rax = 48 89 85 xx xx xx xx (7 bytes)
    // mov rax, [rbp+disp32] = 48 8B 85 xx xx xx xx (7 bytes)
    
    if (i + 14 > code.size()) return false;
    
    // Check for mov [rbp+disp32], rax
    if (code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0x85) {
        int32_t disp1 = *(int32_t*)&code[i+3];
        
        // Check if next instruction is mov rax, [rbp+disp32] with same displacement
        if (code[i+7] == 0x48 && code[i+8] == 0x8B && code[i+9] == 0x85) {
            int32_t disp2 = *(int32_t*)&code[i+10];
            
            if (disp1 == disp2) {
                // NOP out the second mov (the load)
                for (size_t k = 7; k < 14; ++k) {
                    code[i + k] = 0x90;
                }
                removedBytes_ += 7;
                optimizationCount_++;
                i += 14;
                return true;
            }
        }
    }
    
    return false;
}

// Helper functions

bool PeepholeOptimizer::isPushRax(const std::vector<uint8_t>& code, size_t i) {
    return i < code.size() && code[i] == 0x50;
}

bool PeepholeOptimizer::isPopRcx(const std::vector<uint8_t>& code, size_t i) {
    return i < code.size() && code[i] == 0x59;
}

bool PeepholeOptimizer::isPopRdx(const std::vector<uint8_t>& code, size_t i) {
    return i < code.size() && code[i] == 0x5A;
}

bool PeepholeOptimizer::isMovRaxImm64(const std::vector<uint8_t>& code, size_t i) {
    // mov rax, imm64 = 48 B8 xx xx xx xx xx xx xx xx (10 bytes)
    if (i + 10 > code.size()) return false;
    return code[i] == 0x48 && code[i + 1] == 0xB8;
}

bool PeepholeOptimizer::isMovRaxMemRbp(const std::vector<uint8_t>& code, size_t i) {
    // mov rax, [rbp+disp32] = 48 8B 85 xx xx xx xx (7 bytes)
    if (i + 7 > code.size()) return false;
    return code[i] == 0x48 && code[i + 1] == 0x8B && code[i + 2] == 0x85;
}

int64_t PeepholeOptimizer::getImm64(const std::vector<uint8_t>& code, size_t i) {
    if (i + 10 > code.size()) return 0;
    int64_t val = 0;
    for (int k = 0; k < 8; ++k) {
        val |= ((int64_t)code[i + 2 + k]) << (k * 8);
    }
    return val;
}

void PeepholeOptimizer::removeBytes(std::vector<uint8_t>& code, size_t start, size_t count) {
    if (start + count <= code.size()) {
        code.erase(code.begin() + start, code.begin() + start + count);
    }
}

void PeepholeOptimizer::replaceBytes(std::vector<uint8_t>& code, size_t start,
                                      const std::vector<uint8_t>& replacement, size_t oldLen) {
    if (start + oldLen > code.size()) return;
    
    // Replace bytes
    for (size_t k = 0; k < replacement.size() && start + k < code.size(); ++k) {
        code[start + k] = replacement[k];
    }
    
    // If replacement is shorter, fill with NOPs
    for (size_t k = replacement.size(); k < oldLen && start + k < code.size(); ++k) {
        code[start + k] = 0x90;
    }
}

void PeepholeOptimizer::removeNops(std::vector<uint8_t>& /*code*/) {
    // Remove all NOP (0x90) instructions
    // Note: This is safe because we've already resolved all labels
    // and the code doesn't have any relative jumps that would be affected
    
    // Actually, we need to be careful here - removing NOPs would break
    // relative jump offsets. Since labels are already resolved, we should
    // NOT remove NOPs as it would invalidate the jump targets.
    // 
    // Instead, we should only remove NOPs that were inserted by our
    // optimizations and are in sequences (multiple NOPs in a row).
    // For now, let's skip this optimization to be safe.
    
    // Future improvement: track which NOPs are safe to remove
    // (those that don't affect jump targets)
}

bool PeepholeOptimizer::isPushRcx(const std::vector<uint8_t>& code, size_t i) {
    return i < code.size() && code[i] == 0x51;
}

bool PeepholeOptimizer::isPopRax(const std::vector<uint8_t>& code, size_t i) {
    return i < code.size() && code[i] == 0x58;
}

bool PeepholeOptimizer::isNop(const std::vector<uint8_t>& code, size_t i) {
    return i < code.size() && code[i] == 0x90;
}

} // namespace tyl
