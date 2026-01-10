// Tyl Compiler - Native Code Generator Control Flow Statements
// Handles: IfStmt, WhileStmt, ForStmt, MatchStmt

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(IfStmt& node) {
    std::string elseLabel = newLabel("if_else");
    std::string endLabel = newLabel("if_end");
    
    node.condition->accept(*this);
    asm_.test_rax_rax();
    asm_.jz_rel32(elseLabel);
    node.thenBranch->accept(*this);
    
    bool thenTerminates = endsWithTerminator(node.thenBranch.get());
    if (!thenTerminates) {
        asm_.jmp_rel32(endLabel);
    }
    asm_.label(elseLabel);
    
    for (auto& [cond, body] : node.elifBranches) {
        std::string nextLabel = newLabel("elif");
        cond->accept(*this);
        asm_.test_rax_rax();
        asm_.jz_rel32(nextLabel);
        body->accept(*this);
        
        if (!endsWithTerminator(body.get())) {
            asm_.jmp_rel32(endLabel);
        }
        asm_.label(nextLabel);
    }
    
    if (node.elseBranch) {
        node.elseBranch->accept(*this);
    }
    asm_.label(endLabel);
}

void NativeCodeGen::visit(WhileStmt& node) {
    std::string loopLabel = newLabel("while_loop");
    std::string endLabel = newLabel("while_end");
    
    loopStack.push_back({node.label, loopLabel, endLabel});
    
    // Note: We intentionally do NOT clear constVars here.
    // Compile-time constants (like VK_A :: 65) should remain valid inside loops.
    // Only mutable variables that are modified in the loop need special handling,
    // and those are tracked separately.
    
    asm_.label(loopLabel);
    node.condition->accept(*this);
    asm_.test_rax_rax();
    asm_.jz_rel32(endLabel);
    node.body->accept(*this);
    
    if (!endsWithTerminator(node.body.get())) {
        asm_.jmp_rel32(loopLabel);
    }
    asm_.label(endLabel);
    
    loopStack.pop_back();
}

void NativeCodeGen::visit(ForStmt& node) {
    std::string loopLabel = newLabel("for_loop");
    std::string continueLabel = newLabel("for_continue");
    std::string endLabel = newLabel("for_end");
    
    loopStack.push_back({node.label, continueLabel, endLabel});
    
    // Check if loop variable is allocated to a register
    VarRegister loopVarReg = VarRegister::NONE;
    auto regIt = varRegisters_.find(node.var);
    if (regIt != varRegisters_.end()) {
        loopVarReg = regIt->second;
    }
    
    // Handle range expression: for i in 1..10 (INCLUSIVE - includes both start and end)
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        range->start->accept(*this);
        
        if (loopVarReg != VarRegister::NONE) {
            // Store loop variable in register
            emitStoreRaxToVar(node.var);
        } else {
            allocLocal(node.var);
            asm_.mov_mem_rbp_rax(locals[node.var]);
        }
        
        range->end->accept(*this);
        allocLocal("$end");
        asm_.mov_mem_rbp_rax(locals["$end"]);
        
        // Handle step value (by keyword)
        int64_t stepValue = 1;
        bool hasConstStep = false;
        bool hasVarStep = false;
        if (range->step) {
            if (auto* stepLit = dynamic_cast<IntegerLiteral*>(range->step.get())) {
                stepValue = stepLit->value;
                hasConstStep = true;
            } else {
                // Non-constant step - evaluate and store
                range->step->accept(*this);
                allocLocal("$step");
                asm_.mov_mem_rbp_rax(locals["$step"]);
                hasVarStep = true;
            }
        }
        
        constVars.erase(node.var);
        
        asm_.label(loopLabel);
        
        // Load loop variable
        if (loopVarReg != VarRegister::NONE) {
            emitLoadVarToRax(node.var);
        } else {
            asm_.mov_rax_mem_rbp(locals[node.var]);
        }
        asm_.cmp_rax_mem_rbp(locals["$end"]);
        asm_.jg_rel32(endLabel);  // Exit when i > end (inclusive)
        
        node.body->accept(*this);
        
        asm_.label(continueLabel);
        
        // Load, increment, store loop variable
        if (loopVarReg != VarRegister::NONE) {
            emitLoadVarToRax(node.var);
        } else {
            asm_.mov_rax_mem_rbp(locals[node.var]);
        }
        
        if (hasConstStep) {
            asm_.add_rax_imm32(static_cast<int32_t>(stepValue));
        } else if (hasVarStep) {
            asm_.mov_rcx_mem_rbp(locals["$step"]);
            asm_.add_rax_rcx();
        } else {
            asm_.inc_rax();
        }
        
        if (loopVarReg != VarRegister::NONE) {
            emitStoreRaxToVar(node.var);
        } else {
            asm_.mov_mem_rbp_rax(locals[node.var]);
        }
        asm_.jmp_rel32(loopLabel);
        
        asm_.label(endLabel);
        loopStack.pop_back();
        return;
    }
    
    // Handle range() function call (EXCLUSIVE like Python)
    if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (auto* calleeId = dynamic_cast<Identifier*>(call->callee.get())) {
            if (calleeId->name == "range" && call->args.size() >= 1) {
                int64_t stepValue = 1;
                bool hasConstStep = false;
                bool hasVarStep = false;
                
                if (call->args.size() == 1) {
                    // range(end) - start at 0
                    asm_.xor_rax_rax();
                    if (loopVarReg != VarRegister::NONE) {
                        emitStoreRaxToVar(node.var);
                    } else {
                        allocLocal(node.var);
                        asm_.mov_mem_rbp_rax(locals[node.var]);
                    }
                    
                    call->args[0]->accept(*this);
                    allocLocal("$end");
                    asm_.mov_mem_rbp_rax(locals["$end"]);
                } else {
                    // range(start, end) or range(start, end, step)
                    call->args[0]->accept(*this);
                    if (loopVarReg != VarRegister::NONE) {
                        emitStoreRaxToVar(node.var);
                    } else {
                        allocLocal(node.var);
                        asm_.mov_mem_rbp_rax(locals[node.var]);
                    }
                    
                    call->args[1]->accept(*this);
                    allocLocal("$end");
                    asm_.mov_mem_rbp_rax(locals["$end"]);
                    
                    // Handle step value if provided
                    if (call->args.size() >= 3) {
                        if (auto* stepLit = dynamic_cast<IntegerLiteral*>(call->args[2].get())) {
                            stepValue = stepLit->value;
                            hasConstStep = true;
                        } else {
                            // Non-constant step - evaluate and store
                            call->args[2]->accept(*this);
                            allocLocal("$step");
                            asm_.mov_mem_rbp_rax(locals["$step"]);
                            hasVarStep = true;
                        }
                    }
                }
                
                constVars.erase(node.var);
                
                asm_.label(loopLabel);
                
                // Load loop variable
                if (loopVarReg != VarRegister::NONE) {
                    emitLoadVarToRax(node.var);
                } else {
                    asm_.mov_rax_mem_rbp(locals[node.var]);
                }
                asm_.cmp_rax_mem_rbp(locals["$end"]);
                asm_.jge_rel32(endLabel);
                
                node.body->accept(*this);
                
                asm_.label(continueLabel);
                
                // Load, increment, store loop variable
                if (loopVarReg != VarRegister::NONE) {
                    emitLoadVarToRax(node.var);
                } else {
                    asm_.mov_rax_mem_rbp(locals[node.var]);
                }
                
                if (hasConstStep) {
                    asm_.add_rax_imm32(static_cast<int32_t>(stepValue));
                } else if (hasVarStep) {
                    asm_.mov_rcx_mem_rbp(locals["$step"]);
                    asm_.add_rax_rcx();
                } else {
                    asm_.inc_rax();
                }
                
                if (loopVarReg != VarRegister::NONE) {
                    emitStoreRaxToVar(node.var);
                } else {
                    asm_.mov_mem_rbp_rax(locals[node.var]);
                }
                asm_.jmp_rel32(loopLabel);
                
                asm_.label(endLabel);
                loopStack.pop_back();
                return;
            }
        }
    }
    
    // Handle iteration over list variable
    if (auto* ident = dynamic_cast<Identifier*>(node.iterable.get())) {
        auto sizeIt = listSizes.find(ident->name);
        
        if (sizeIt != listSizes.end() && sizeIt->second > 0) {
            size_t listSize = sizeIt->second;
            
            node.iterable->accept(*this);
            allocLocal("$for_list_ptr");
            asm_.mov_mem_rbp_rax(locals["$for_list_ptr"]);
            
            allocLocal("$for_idx");
            asm_.xor_rax_rax();
            asm_.mov_mem_rbp_rax(locals["$for_idx"]);
            
            allocLocal("$for_list_size");
            asm_.mov_rax_imm64((int64_t)listSize);
            asm_.mov_mem_rbp_rax(locals["$for_list_size"]);
            
            if (loopVarReg == VarRegister::NONE) {
                allocLocal(node.var);
            }
            constVars.erase(node.var);
            
            asm_.label(loopLabel);
            
            asm_.mov_rax_mem_rbp(locals["$for_idx"]);
            asm_.cmp_rax_mem_rbp(locals["$for_list_size"]);
            asm_.jge_rel32(endLabel);
            
            asm_.mov_rcx_mem_rbp(locals["$for_list_ptr"]);
            asm_.mov_rax_mem_rbp(locals["$for_idx"]);
            asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
            asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
            asm_.add_rax_rcx();
            asm_.mov_rax_mem_rax();
            
            if (loopVarReg != VarRegister::NONE) {
                emitStoreRaxToVar(node.var);
            } else {
                asm_.mov_mem_rbp_rax(locals[node.var]);
            }
            
            node.body->accept(*this);
            
            asm_.label(continueLabel);
            asm_.mov_rax_mem_rbp(locals["$for_idx"]);
            asm_.inc_rax();
            asm_.mov_mem_rbp_rax(locals["$for_idx"]);
            asm_.jmp_rel32(loopLabel);
            
            asm_.label(endLabel);
            loopStack.pop_back();
            return;
        }
    }
    
    // Fallback: iterate over list with runtime size
    node.iterable->accept(*this);
    allocLocal("$for_list_ptr");
    asm_.mov_mem_rbp_rax(locals["$for_list_ptr"]);
    
    allocLocal("$for_idx");
    asm_.xor_rax_rax();
    asm_.mov_mem_rbp_rax(locals["$for_idx"]);
    
    allocLocal("$for_list_size");
    asm_.mov_rax_mem_rbp(locals["$for_list_ptr"]);
    asm_.mov_rax_mem_rax();
    asm_.mov_mem_rbp_rax(locals["$for_list_size"]);
    
    if (loopVarReg == VarRegister::NONE) {
        allocLocal(node.var);
    }
    constVars.erase(node.var);
    
    asm_.label(loopLabel);
    
    asm_.mov_rax_mem_rbp(locals["$for_idx"]);
    asm_.cmp_rax_mem_rbp(locals["$for_list_size"]);
    asm_.jge_rel32(endLabel);
    
    asm_.mov_rcx_mem_rbp(locals["$for_list_ptr"]);
    asm_.add_rcx_imm32(8);
    asm_.mov_rax_mem_rbp(locals["$for_idx"]);
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
    asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
    asm_.add_rax_rcx();
    asm_.mov_rax_mem_rax();
    
    if (loopVarReg != VarRegister::NONE) {
        emitStoreRaxToVar(node.var);
    } else {
        asm_.mov_mem_rbp_rax(locals[node.var]);
    }
    
    node.body->accept(*this);
    
    asm_.label(continueLabel);
    asm_.mov_rax_mem_rbp(locals["$for_idx"]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals["$for_idx"]);
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(endLabel);
    loopStack.pop_back();
}

void NativeCodeGen::visit(MatchStmt& node) {
    node.value->accept(*this);
    allocLocal("$match_val");
    asm_.mov_mem_rbp_rax(locals["$match_val"]);
    
    std::string endLabel = newLabel("match_end");
    
    for (size_t i = 0; i < node.cases.size(); i++) {
        auto& matchCase = node.cases[i];
        std::string nextCase = newLabel("match_case");
        
        // Check pattern
        if (auto* intLit = dynamic_cast<IntegerLiteral*>(matchCase.pattern.get())) {
            asm_.mov_rax_mem_rbp(locals["$match_val"]);
            asm_.cmp_rax_imm32((int32_t)intLit->value);
            asm_.jnz_rel32(nextCase);
        } else if (auto* boolLit = dynamic_cast<BoolLiteral*>(matchCase.pattern.get())) {
            asm_.mov_rax_mem_rbp(locals["$match_val"]);
            asm_.cmp_rax_imm32(boolLit->value ? 1 : 0);
            asm_.jnz_rel32(nextCase);
        } else if (auto* rangeExpr = dynamic_cast<RangeExpr*>(matchCase.pattern.get())) {
            // Range pattern: match value in start..end (inclusive)
            // Check: value >= start AND value <= end
            std::string inRange = newLabel("range_check");
            
            // First check: value >= start
            asm_.mov_rax_mem_rbp(locals["$match_val"]);
            if (auto* startLit = dynamic_cast<IntegerLiteral*>(rangeExpr->start.get())) {
                asm_.cmp_rax_imm32((int32_t)startLit->value);
            } else {
                // Evaluate start expression
                asm_.push_rax();
                rangeExpr->start->accept(*this);
                asm_.mov_rcx_rax();
                asm_.pop_rax();
                asm_.cmp_rax_rcx();
            }
            asm_.jl_rel32(nextCase);  // value < start, skip
            
            // Second check: value <= end
            asm_.mov_rax_mem_rbp(locals["$match_val"]);
            if (auto* endLit = dynamic_cast<IntegerLiteral*>(rangeExpr->end.get())) {
                asm_.cmp_rax_imm32((int32_t)endLit->value);
            } else {
                // Evaluate end expression
                asm_.push_rax();
                rangeExpr->end->accept(*this);
                asm_.mov_rcx_rax();
                asm_.pop_rax();
                asm_.cmp_rax_rcx();
            }
            asm_.jg_rel32(nextCase);  // value > end, skip
            
        } else if (auto* ident = dynamic_cast<Identifier*>(matchCase.pattern.get())) {
            if (ident->name == "_") {
                // Wildcard - always matches
            } else {
                // Bind variable
                asm_.mov_rax_mem_rbp(locals["$match_val"]);
                allocLocal(ident->name);
                asm_.mov_mem_rbp_rax(locals[ident->name]);
            }
        }
        
        // Check guard if present
        if (matchCase.guard) {
            matchCase.guard->accept(*this);
            asm_.test_rax_rax();
            asm_.jz_rel32(nextCase);
        }
        
        // Execute body
        matchCase.body->accept(*this);
        
        if (!endsWithTerminator(matchCase.body.get())) {
            asm_.jmp_rel32(endLabel);
        }
        
        asm_.label(nextCase);
    }
    
    // Handle default case
    if (node.defaultCase) {
        node.defaultCase->accept(*this);
    }
    
    asm_.label(endLabel);
}

} // namespace tyl
