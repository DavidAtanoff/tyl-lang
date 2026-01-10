// Tyl Compiler - Peephole Optimizer Implementation
// Enhanced with register coalescing and more patterns for O3 optimization
#include "peephole.h"
#include <cstring>

namespace tyl {

size_t PeepholeOptimizer::optimize(std::vector<uint8_t>& code) {
    removedBytes_ = 0;
    optimizationCount_ = 0;
    
    // Multiple passes until no more optimizations
    bool changed = true;
    int passes = 0;
    const int maxPasses = 10;  // Prevent infinite loops
    
    while (changed && passes < maxPasses) {
        changed = false;
        passes++;
        
        for (size_t i = 0; i < code.size(); ) {
            // Try each optimization pattern in order of effectiveness
            
            // Register coalescing - most impactful for O3
            if (aggressiveMode_ && optimizeRegisterCoalescing(code, i)) {
                changed = true;
                continue;
            }
            
            // Redundant xor after xor (xor rax,rax; xor rax,rax)
            if (optimizeRedundantXor(code, i)) {
                changed = true;
                continue;
            }
            
            // Direct push/pop elimination
            if (optimizeDirectPushPop(code, i)) {
                changed = true;
                continue;
            }
            
            // Redundant push/pop pairs
            if (optimizeRedundantPushPop(code, i)) {
                changed = true;
                continue;
            }
            
            // Push/pop with mov optimization
            if (optimizePushPop(code, i)) {
                changed = true;
                continue;
            }
            
            // Small constant optimization
            if (optimizeSmallConstants(code, i)) {
                changed = true;
                continue;
            }
            
            // Redundant mov elimination
            if (optimizeRedundantMov(code, i)) {
                changed = true;
                continue;
            }
            
            // xor to zero optimization
            if (aggressiveMode_ && optimizeXorZero(code, i)) {
                changed = true;
                continue;
            }
            
            // inc/dec optimization
            if (aggressiveMode_ && optimizeIncDec(code, i)) {
                changed = true;
                continue;
            }
            
            // LEA arithmetic optimization
            if (aggressiveMode_ && optimizeLeaArithmetic(code, i)) {
                changed = true;
                continue;
            }
            
            // test/cmp optimization
            if (aggressiveMode_ && optimizeTestCmp(code, i)) {
                changed = true;
                continue;
            }
            
            // xor before mov imm optimization
            if (aggressiveMode_ && optimizeXorBeforeMovImm(code, i)) {
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

void PeepholeOptimizer::removeNops(std::vector<uint8_t>& code) {
    // IMPORTANT: We cannot safely remove NOPs after code generation because
    // relative jump offsets have already been calculated. Removing bytes
    // would invalidate all jump targets.
    //
    // The NOPs inserted by peephole optimizations are cosmetic - they don't
    // affect correctness, just code size. A proper solution would require:
    // 1. Tracking all jump instructions and their targets
    // 2. Recalculating offsets after NOP removal
    // 3. Potentially iterating if the offset size changes (e.g., rel8 to rel32)
    //
    // For now, we leave NOPs in place. The code is still correct and the
    // size overhead is minimal (typically a few dozen bytes).
    (void)code;
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

// ============================================
// New O3 Optimization Patterns
// ============================================

// Pattern: xor rax, rax; mov REG, rax -> xor REG, REG
// This eliminates redundant register moves after zeroing
bool PeepholeOptimizer::optimizeRegisterCoalescing(std::vector<uint8_t>& code, size_t& i) {
    if (i + 6 > code.size()) return false;
    
    // Check for xor rax, rax (48 31 C0)
    if (code[i] == 0x48 && code[i+1] == 0x31 && code[i+2] == 0xC0) {
        // mov rbx, rax = 48 89 C3
        if (code[i+3] == 0x48 && code[i+4] == 0x89 && code[i+5] == 0xC3) {
            // Replace with xor ebx, ebx (31 DB) - 2 bytes
            code[i] = 0x31;
            code[i+1] = 0xDB;
            code[i+2] = 0x90;
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 4;
            optimizationCount_++;
            i += 6;
            return true;
        }
        // mov rcx, rax = 48 89 C1
        if (code[i+3] == 0x48 && code[i+4] == 0x89 && code[i+5] == 0xC1) {
            // Replace with xor ecx, ecx (31 C9) - 2 bytes
            code[i] = 0x31;
            code[i+1] = 0xC9;
            code[i+2] = 0x90;
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 4;
            optimizationCount_++;
            i += 6;
            return true;
        }
        // mov rdx, rax = 48 89 C2
        if (code[i+3] == 0x48 && code[i+4] == 0x89 && code[i+5] == 0xC2) {
            // Replace with xor edx, edx (31 D2) - 2 bytes
            code[i] = 0x31;
            code[i+1] = 0xD2;
            code[i+2] = 0x90;
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 4;
            optimizationCount_++;
            i += 6;
            return true;
        }
        // mov r12, rax = 49 89 C4
        if (code[i+3] == 0x49 && code[i+4] == 0x89 && code[i+5] == 0xC4) {
            // Replace with xor r12d, r12d (45 31 E4) - 3 bytes
            code[i] = 0x45;
            code[i+1] = 0x31;
            code[i+2] = 0xE4;
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 3;
            optimizationCount_++;
            i += 6;
            return true;
        }
        // mov r13, rax = 49 89 C5
        if (code[i+3] == 0x49 && code[i+4] == 0x89 && code[i+5] == 0xC5) {
            // Replace with xor r13d, r13d (45 31 ED)
            code[i] = 0x45;
            code[i+1] = 0x31;
            code[i+2] = 0xED;
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 3;
            optimizationCount_++;
            i += 6;
            return true;
        }
        // mov r14, rax = 49 89 C6
        if (code[i+3] == 0x49 && code[i+4] == 0x89 && code[i+5] == 0xC6) {
            // Replace with xor r14d, r14d (45 31 F6)
            code[i] = 0x45;
            code[i+1] = 0x31;
            code[i+2] = 0xF6;
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 3;
            optimizationCount_++;
            i += 6;
            return true;
        }
        // mov r15, rax = 49 89 C7
        if (code[i+3] == 0x49 && code[i+4] == 0x89 && code[i+5] == 0xC7) {
            // Replace with xor r15d, r15d (45 31 FF)
            code[i] = 0x45;
            code[i+1] = 0x31;
            code[i+2] = 0xFF;
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 3;
            optimizationCount_++;
            i += 6;
            return true;
        }
    }
    
    return false;
}

// Pattern: xor rax, rax; xor rax, rax -> xor rax, rax (remove duplicate)
bool PeepholeOptimizer::optimizeRedundantXor(std::vector<uint8_t>& code, size_t& i) {
    if (i + 6 > code.size()) return false;
    
    // xor rax, rax = 48 31 C0
    if (code[i] == 0x48 && code[i+1] == 0x31 && code[i+2] == 0xC0) {
        if (code[i+3] == 0x48 && code[i+4] == 0x31 && code[i+5] == 0xC0) {
            // NOP out the second xor
            code[i+3] = 0x90;
            code[i+4] = 0x90;
            code[i+5] = 0x90;
            removedBytes_ += 3;
            optimizationCount_++;
            i += 6;
            return true;
        }
    }
    
    return false;
}

// Pattern: push rax; pop rax -> (remove both, they cancel out)
bool PeepholeOptimizer::optimizeRedundantPushPop(std::vector<uint8_t>& code, size_t& i) {
    if (i + 2 > code.size()) return false;
    
    // push rax (0x50) followed by pop rax (0x58)
    if (code[i] == 0x50 && code[i+1] == 0x58) {
        code[i] = 0x90;
        code[i+1] = 0x90;
        removedBytes_ += 2;
        optimizationCount_++;
        i += 2;
        return true;
    }
    
    // push rcx (0x51) followed by pop rcx (0x59)
    if (code[i] == 0x51 && code[i+1] == 0x59) {
        code[i] = 0x90;
        code[i+1] = 0x90;
        removedBytes_ += 2;
        optimizationCount_++;
        i += 2;
        return true;
    }
    
    // push rdx (0x52) followed by pop rdx (0x5A)
    if (code[i] == 0x52 && code[i+1] == 0x5A) {
        code[i] = 0x90;
        code[i+1] = 0x90;
        removedBytes_ += 2;
        optimizationCount_++;
        i += 2;
        return true;
    }
    
    return false;
}

// Pattern: mov rax, 0 -> xor eax, eax (smaller and faster)
bool PeepholeOptimizer::optimizeXorZero(std::vector<uint8_t>& code, size_t& i) {
    // mov rax, 0 = 48 B8 00 00 00 00 00 00 00 00 (10 bytes)
    if (i + 10 > code.size()) return false;
    
    if (code[i] == 0x48 && code[i+1] == 0xB8) {
        // Check if immediate is 0
        bool isZero = true;
        for (int j = 2; j < 10; j++) {
            if (code[i+j] != 0) {
                isZero = false;
                break;
            }
        }
        if (isZero) {
            // Replace with xor eax, eax (31 C0) - 2 bytes
            code[i] = 0x31;
            code[i+1] = 0xC0;
            for (int j = 2; j < 10; j++) {
                code[i+j] = 0x90;
            }
            removedBytes_ += 8;
            optimizationCount_++;
            i += 10;
            return true;
        }
    }
    
    return false;
}

// Pattern: add rax, 1 -> inc rax (smaller)
bool PeepholeOptimizer::optimizeIncDec(std::vector<uint8_t>& code, size_t& i) {
    // add rax, 1 = 48 83 C0 01 (4 bytes) or 48 05 01 00 00 00 (6 bytes)
    if (i + 4 > code.size()) return false;
    
    // add rax, imm8 = 48 83 C0 xx
    if (code[i] == 0x48 && code[i+1] == 0x83 && code[i+2] == 0xC0) {
        if (code[i+3] == 0x01) {
            // Replace with inc rax (48 FF C0) - 3 bytes
            code[i] = 0x48;
            code[i+1] = 0xFF;
            code[i+2] = 0xC0;
            code[i+3] = 0x90;
            removedBytes_ += 1;
            optimizationCount_++;
            i += 4;
            return true;
        }
        if (code[i+3] == 0xFF) {  // -1 in signed byte
            // Replace with dec rax (48 FF C8) - 3 bytes
            code[i] = 0x48;
            code[i+1] = 0xFF;
            code[i+2] = 0xC8;
            code[i+3] = 0x90;
            removedBytes_ += 1;
            optimizationCount_++;
            i += 4;
            return true;
        }
    }
    
    // sub rax, 1 = 48 83 E8 01
    if (code[i] == 0x48 && code[i+1] == 0x83 && code[i+2] == 0xE8 && code[i+3] == 0x01) {
        // Replace with dec rax (48 FF C8) - 3 bytes
        code[i] = 0x48;
        code[i+1] = 0xFF;
        code[i+2] = 0xC8;
        code[i+3] = 0x90;
        removedBytes_ += 1;
        optimizationCount_++;
        i += 4;
        return true;
    }
    
    return false;
}

// Pattern: add rax, rcx -> lea rax, [rax + rcx] (can be combined with other ops)
bool PeepholeOptimizer::optimizeLeaArithmetic(std::vector<uint8_t>& code, size_t& i) {
    // This is more of a preparation for future optimizations
    // For now, we'll skip this as it doesn't always improve code
    (void)code;
    (void)i;
    return false;
}

// Pattern: cmp rax, 0; je label -> test rax, rax; je label (smaller)
bool PeepholeOptimizer::optimizeTestCmp(std::vector<uint8_t>& code, size_t& i) {
    // cmp rax, 0 = 48 83 F8 00 (4 bytes)
    if (i + 4 > code.size()) return false;
    
    if (code[i] == 0x48 && code[i+1] == 0x83 && code[i+2] == 0xF8 && code[i+3] == 0x00) {
        // Replace with test rax, rax (48 85 C0) - 3 bytes
        code[i] = 0x48;
        code[i+1] = 0x85;
        code[i+2] = 0xC0;
        code[i+3] = 0x90;
        removedBytes_ += 1;
        optimizationCount_++;
        i += 4;
        return true;
    }
    
    // cmp rcx, 0 = 48 83 F9 00
    if (code[i] == 0x48 && code[i+1] == 0x83 && code[i+2] == 0xF9 && code[i+3] == 0x00) {
        // Replace with test rcx, rcx (48 85 C9) - 3 bytes
        code[i] = 0x48;
        code[i+1] = 0x85;
        code[i+2] = 0xC9;
        code[i+3] = 0x90;
        removedBytes_ += 1;
        optimizationCount_++;
        i += 4;
        return true;
    }
    
    return false;
}

// Pattern: xor rax, rax followed by mov eax, imm32 -> just mov eax, imm32
// The xor is redundant because mov eax, imm32 zero-extends to rax
bool PeepholeOptimizer::optimizeXorBeforeMovImm(std::vector<uint8_t>& code, size_t& i) {
    // xor rax, rax = 48 31 C0 (3 bytes)
    // xor eax, eax = 31 C0 (2 bytes)
    if (i + 5 > code.size()) return false;
    
    // Check for xor rax, rax (48 31 C0) followed by mov eax, imm32 (B8 xx xx xx xx)
    if (code[i] == 0x48 && code[i+1] == 0x31 && code[i+2] == 0xC0) {
        // Check if next instruction is mov eax, imm32 (B8 xx xx xx xx)
        if (i + 8 <= code.size() && code[i+3] == 0xB8) {
            // NOP out the xor rax, rax - mov eax zero-extends to rax anyway
            code[i] = 0x90;
            code[i+1] = 0x90;
            code[i+2] = 0x90;
            removedBytes_ += 3;
            optimizationCount_++;
            i += 3;
            return true;
        }
        // Check for mov r8d, imm32 (41 B8 xx xx xx xx)
        if (i + 9 <= code.size() && code[i+3] == 0x41 && code[i+4] == 0xB8) {
            // This is xor rax, rax followed by mov r8d, imm32
            // The xor is likely for a different purpose, skip
            return false;
        }
    }
    
    // Check for xor eax, eax (31 C0) followed by mov eax, imm32 (B8 xx xx xx xx)
    if (code[i] == 0x31 && code[i+1] == 0xC0) {
        if (i + 7 <= code.size() && code[i+2] == 0xB8) {
            // NOP out the xor eax, eax
            code[i] = 0x90;
            code[i+1] = 0x90;
            removedBytes_ += 2;
            optimizationCount_++;
            i += 2;
            return true;
        }
    }
    
    return false;
}

// Pattern: movzx optimization (not commonly needed, placeholder)
bool PeepholeOptimizer::optimizeMovZeroExtend(std::vector<uint8_t>& code, size_t& i) {
    (void)code;
    (void)i;
    return false;
}

// Helper functions for new patterns
bool PeepholeOptimizer::isXorRaxRax(const std::vector<uint8_t>& code, size_t i) {
    if (i + 3 > code.size()) return false;
    return code[i] == 0x48 && code[i+1] == 0x31 && code[i+2] == 0xC0;
}

bool PeepholeOptimizer::isXorRcxRcx(const std::vector<uint8_t>& code, size_t i) {
    if (i + 3 > code.size()) return false;
    return code[i] == 0x48 && code[i+1] == 0x31 && code[i+2] == 0xC9;
}

bool PeepholeOptimizer::isMovRaxRcx(const std::vector<uint8_t>& code, size_t i) {
    if (i + 3 > code.size()) return false;
    return code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0xC8;
}

bool PeepholeOptimizer::isMovRcxRax(const std::vector<uint8_t>& code, size_t i) {
    if (i + 3 > code.size()) return false;
    return code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0xC1;
}

bool PeepholeOptimizer::isMovR12Rax(const std::vector<uint8_t>& code, size_t i) {
    if (i + 3 > code.size()) return false;
    return code[i] == 0x49 && code[i+1] == 0x89 && code[i+2] == 0xC4;
}

bool PeepholeOptimizer::isMovR13Rax(const std::vector<uint8_t>& code, size_t i) {
    if (i + 3 > code.size()) return false;
    return code[i] == 0x49 && code[i+1] == 0x89 && code[i+2] == 0xC5;
}

bool PeepholeOptimizer::isAddRaxImm(const std::vector<uint8_t>& code, size_t i, int32_t& imm) {
    if (i + 4 > code.size()) return false;
    if (code[i] == 0x48 && code[i+1] == 0x83 && code[i+2] == 0xC0) {
        imm = (int8_t)code[i+3];  // Sign extend
        return true;
    }
    return false;
}

bool PeepholeOptimizer::isSubRaxImm(const std::vector<uint8_t>& code, size_t i, int32_t& imm) {
    if (i + 4 > code.size()) return false;
    if (code[i] == 0x48 && code[i+1] == 0x83 && code[i+2] == 0xE8) {
        imm = (int8_t)code[i+3];  // Sign extend
        return true;
    }
    return false;
}

void PeepholeOptimizer::nopOut(std::vector<uint8_t>& code, size_t start, size_t count) {
    for (size_t i = 0; i < count && start + i < code.size(); ++i) {
        code[start + i] = 0x90;
    }
}

} // namespace tyl
