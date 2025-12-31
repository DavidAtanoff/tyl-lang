// Flex Compiler - Native Code Generator Statement Visitors
// Handles: if, while, for, match, return, break, continue, blocks, etc.

#include "codegen_base.h"

namespace flex {

void NativeCodeGen::visit(ExprStmt& node) {
    node.expr->accept(*this);
}

void NativeCodeGen::visit(VarDecl& node) {
    if (node.initializer) {
        // Check if this is a float variable
        bool isFloat = isFloatExpression(node.initializer.get());
        
        // Only track as constant if not mutable
        if (!node.isMutable) {
            if (isFloat) {
                double floatVal;
                if (tryEvalConstantFloat(node.initializer.get(), floatVal)) {
                    constFloatVars[node.name] = floatVal;
                }
            } else {
                int64_t intVal;
                if (tryEvalConstant(node.initializer.get(), intVal)) {
                    constVars[node.name] = intVal;
                }
            }
            std::string strVal;
            if (tryEvalConstantString(node.initializer.get(), strVal)) {
                constStrVars[node.name] = strVal;
            }
        }
        
        // Track float variables for codegen
        if (isFloat) {
            floatVars.insert(node.name);
        }
        
        if (dynamic_cast<StringLiteral*>(node.initializer.get()) ||
            dynamic_cast<InterpolatedString*>(node.initializer.get())) {
            if (constStrVars.find(node.name) == constStrVars.end()) {
                constStrVars[node.name] = "";
            }
        }
        // Track list variables
        if (auto* list = dynamic_cast<ListExpr*>(node.initializer.get())) {
            listSizes[node.name] = list->elements.size();
            std::vector<int64_t> values;
            bool allConst = true;
            for (auto& elem : list->elements) {
                int64_t val;
                if (tryEvalConstant(elem.get(), val)) {
                    values.push_back(val);
                } else {
                    allConst = false;
                    break;
                }
            }
            if (allConst) {
                constListVars[node.name] = values;
            }
        }
        node.initializer->accept(*this);
        
        // Check if variable has a function-local register allocated
        auto regIt = varRegisters_.find(node.name);
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            // Store to register
            if (isFloat && lastExprWasFloat_) {
                // Float value in xmm0, need to move to GPR first
                asm_.movq_rax_xmm0();
            }
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
            return;
        }
        
        // Check if variable has a global register allocated (top-level code)
        auto globalRegIt = globalVarRegisters_.find(node.name);
        if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
            // Store to global register
            if (isFloat && lastExprWasFloat_) {
                asm_.movq_rax_xmm0();
            }
            switch (globalRegIt->second) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
            return;
        }
        
        // Store float in stack using xmm0 if it's a float
        if (isFloat && lastExprWasFloat_) {
            allocLocal(node.name);
            asm_.movsd_mem_rbp_xmm0(locals[node.name]);
            return;
        }
    } else {
        asm_.xor_rax_rax();
    }
    
    // Check if variable has a function-local register allocated
    auto regIt = varRegisters_.find(node.name);
    if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
        switch (regIt->second) {
            case VarRegister::RBX: asm_.mov_rbx_rax(); break;
            case VarRegister::R12: asm_.mov_r12_rax(); break;
            case VarRegister::R13: asm_.mov_r13_rax(); break;
            case VarRegister::R14: asm_.mov_r14_rax(); break;
            case VarRegister::R15: asm_.mov_r15_rax(); break;
            default: break;
        }
        return;
    }
    
    // Check if variable has a global register allocated
    auto globalRegIt = globalVarRegisters_.find(node.name);
    if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
        switch (globalRegIt->second) {
            case VarRegister::RBX: asm_.mov_rbx_rax(); break;
            case VarRegister::R12: asm_.mov_r12_rax(); break;
            case VarRegister::R13: asm_.mov_r13_rax(); break;
            case VarRegister::R14: asm_.mov_r14_rax(); break;
            case VarRegister::R15: asm_.mov_r15_rax(); break;
            default: break;
        }
        return;
    }
    
    // Fall back to stack allocation
    allocLocal(node.name);
    asm_.mov_mem_rbp_rax(locals[node.name]);
}

void NativeCodeGen::visit(DestructuringDecl& node) {
    if (node.kind == DestructuringDecl::Kind::TUPLE) {
        if (auto* list = dynamic_cast<ListExpr*>(node.initializer.get())) {
            for (size_t i = 0; i < node.names.size() && i < list->elements.size(); i++) {
                list->elements[i]->accept(*this);
                allocLocal(node.names[i]);
                asm_.mov_mem_rbp_rax(locals[node.names[i]]);
                
                int64_t val;
                if (tryEvalConstant(list->elements[i].get(), val)) {
                    constVars[node.names[i]] = val;
                }
            }
            return;
        }
    }
    
    if (node.kind == DestructuringDecl::Kind::RECORD) {
        if (auto* rec = dynamic_cast<RecordExpr*>(node.initializer.get())) {
            std::map<std::string, Expression*> fieldMap;
            for (auto& [name, expr] : rec->fields) {
                fieldMap[name] = expr.get();
            }
            
            for (const std::string& name : node.names) {
                auto it = fieldMap.find(name);
                if (it != fieldMap.end()) {
                    it->second->accept(*this);
                    
                    int64_t val;
                    if (tryEvalConstant(it->second, val)) {
                        constVars[name] = val;
                    }
                    std::string strVal;
                    if (tryEvalConstantString(it->second, strVal)) {
                        constStrVars[name] = strVal;
                    } else if (dynamic_cast<StringLiteral*>(it->second) ||
                               dynamic_cast<InterpolatedString*>(it->second)) {
                        constStrVars[name] = "";
                    }
                } else {
                    asm_.xor_rax_rax();
                }
                allocLocal(name);
                asm_.mov_mem_rbp_rax(locals[name]);
            }
            return;
        }
    }
    
    node.initializer->accept(*this);
    
    allocLocal("$destruct_base");
    asm_.mov_mem_rbp_rax(locals["$destruct_base"]);
    
    for (size_t i = 0; i < node.names.size(); i++) {
        asm_.mov_rax_mem_rbp(locals["$destruct_base"]);
        
        if (i > 0) {
            asm_.mov_rcx_imm64(i * 8);
            asm_.add_rax_rcx();
        }
        
        asm_.mov_rax_mem_rax();
        
        allocLocal(node.names[i]);
        asm_.mov_mem_rbp_rax(locals[node.names[i]]);
    }
}

void NativeCodeGen::visit(AssignStmt& node) {
    bool isFloat = false;
    
    if (auto* id = dynamic_cast<Identifier*>(node.target.get())) {
        isFloat = floatVars.count(id->name) > 0 || isFloatExpression(node.value.get());
        
        if (node.op == TokenType::ASSIGN) {
            if (isFloat) {
                double floatVal;
                if (tryEvalConstantFloat(node.value.get(), floatVal)) {
                    constFloatVars[id->name] = floatVal;
                } else {
                    constFloatVars.erase(id->name);
                }
                floatVars.insert(id->name);
            } else {
                int64_t intVal;
                if (tryEvalConstant(node.value.get(), intVal)) {
                    constVars[id->name] = intVal;
                } else {
                    constVars.erase(id->name);
                }
            }
            std::string strVal;
            if (tryEvalConstantString(node.value.get(), strVal)) {
                constStrVars[id->name] = strVal;
            } else {
                constStrVars.erase(id->name);
            }
        } else {
            constVars.erase(id->name);
            constStrVars.erase(id->name);
            constFloatVars.erase(id->name);
        }
    }
    
    // OPTIMIZATION: Check if value is a small constant for compound assignments
    int64_t constVal;
    bool valueIsConst = tryEvalConstant(node.value.get(), constVal);
    bool valueIsSmall = valueIsConst && constVal >= INT32_MIN && constVal <= INT32_MAX;
    
    if (auto* id = dynamic_cast<Identifier*>(node.target.get())) {
        // Check if variable is in a function-local register
        auto regIt = varRegisters_.find(id->name);
        // Also check global registers
        auto globalRegIt = globalVarRegisters_.find(id->name);
        
        VarRegister reg = VarRegister::NONE;
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            reg = regIt->second;
        } else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
            reg = globalRegIt->second;
        }
        
        if (reg != VarRegister::NONE) {
            
            if (!isFloat) {
                // OPTIMIZATION: For compound assignments with small constants, use immediate instructions
                if (valueIsSmall && (node.op == TokenType::PLUS_ASSIGN || node.op == TokenType::MINUS_ASSIGN)) {
                    // Load current value from register to rax
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                        case VarRegister::R12: asm_.mov_rax_r12(); break;
                        case VarRegister::R13: asm_.mov_rax_r13(); break;
                        case VarRegister::R14: asm_.mov_rax_r14(); break;
                        case VarRegister::R15: asm_.mov_rax_r15(); break;
                        default: break;
                    }
                    
                    if (node.op == TokenType::PLUS_ASSIGN) {
                        // add rax, imm32
                        asm_.code.push_back(0x48); asm_.code.push_back(0x05);
                        asm_.code.push_back(constVal & 0xFF);
                        asm_.code.push_back((constVal >> 8) & 0xFF);
                        asm_.code.push_back((constVal >> 16) & 0xFF);
                        asm_.code.push_back((constVal >> 24) & 0xFF);
                    } else {
                        // sub rax, imm32
                        asm_.code.push_back(0x48); asm_.code.push_back(0x2D);
                        asm_.code.push_back(constVal & 0xFF);
                        asm_.code.push_back((constVal >> 8) & 0xFF);
                        asm_.code.push_back((constVal >> 16) & 0xFF);
                        asm_.code.push_back((constVal >> 24) & 0xFF);
                    }
                    
                    // Store result back to register
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                        case VarRegister::R12: asm_.mov_r12_rax(); break;
                        case VarRegister::R13: asm_.mov_r13_rax(); break;
                        case VarRegister::R14: asm_.mov_r14_rax(); break;
                        case VarRegister::R15: asm_.mov_r15_rax(); break;
                        default: break;
                    }
                    return;
                }
                
                // Non-constant compound assignment - use push/pop
                node.value->accept(*this);
                
                if (node.op == TokenType::PLUS_ASSIGN) {
                    asm_.push_rax();
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                        case VarRegister::R12: asm_.mov_rax_r12(); break;
                        case VarRegister::R13: asm_.mov_rax_r13(); break;
                        case VarRegister::R14: asm_.mov_rax_r14(); break;
                        case VarRegister::R15: asm_.mov_rax_r15(); break;
                        default: break;
                    }
                    asm_.pop_rcx();
                    asm_.add_rax_rcx();
                } else if (node.op == TokenType::MINUS_ASSIGN) {
                    asm_.push_rax();
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                        case VarRegister::R12: asm_.mov_rax_r12(); break;
                        case VarRegister::R13: asm_.mov_rax_r13(); break;
                        case VarRegister::R14: asm_.mov_rax_r14(); break;
                        case VarRegister::R15: asm_.mov_rax_r15(); break;
                        default: break;
                    }
                    asm_.pop_rcx();
                    asm_.sub_rax_rcx();
                } else if (node.op == TokenType::STAR_ASSIGN) {
                    asm_.push_rax();
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                        case VarRegister::R12: asm_.mov_rax_r12(); break;
                        case VarRegister::R13: asm_.mov_rax_r13(); break;
                        case VarRegister::R14: asm_.mov_rax_r14(); break;
                        case VarRegister::R15: asm_.mov_rax_r15(); break;
                        default: break;
                    }
                    asm_.pop_rcx();
                    asm_.imul_rax_rcx();
                } else if (node.op == TokenType::SLASH_ASSIGN) {
                    asm_.mov_rcx_rax();  // rcx = divisor
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                        case VarRegister::R12: asm_.mov_rax_r12(); break;
                        case VarRegister::R13: asm_.mov_rax_r13(); break;
                        case VarRegister::R14: asm_.mov_rax_r14(); break;
                        case VarRegister::R15: asm_.mov_rax_r15(); break;
                        default: break;
                    }
                    asm_.cqo();
                    asm_.idiv_rcx();
                } else if (node.op == TokenType::ASSIGN) {
                    // Simple assignment - value already in rax
                }
                // Store result to register
                switch (reg) {
                    case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                    case VarRegister::R12: asm_.mov_r12_rax(); break;
                    case VarRegister::R13: asm_.mov_r13_rax(); break;
                    case VarRegister::R14: asm_.mov_r14_rax(); break;
                    case VarRegister::R15: asm_.mov_r15_rax(); break;
                    default: break;
                }
                return;
            }
            
            // Float assignment to register
            node.value->accept(*this);
            if (isFloat && lastExprWasFloat_) {
                asm_.movq_rax_xmm0();
                
                if (node.op == TokenType::PLUS_ASSIGN || node.op == TokenType::MINUS_ASSIGN ||
                    node.op == TokenType::STAR_ASSIGN || node.op == TokenType::SLASH_ASSIGN) {
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                        case VarRegister::R12: asm_.mov_rax_r12(); break;
                        case VarRegister::R13: asm_.mov_rax_r13(); break;
                        case VarRegister::R14: asm_.mov_rax_r14(); break;
                        case VarRegister::R15: asm_.mov_rax_r15(); break;
                        default: break;
                    }
                    asm_.movq_xmm1_rcx();
                }
                
                switch (reg) {
                    case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                    case VarRegister::R12: asm_.mov_r12_rax(); break;
                    case VarRegister::R13: asm_.mov_r13_rax(); break;
                    case VarRegister::R14: asm_.mov_r14_rax(); break;
                    case VarRegister::R15: asm_.mov_r15_rax(); break;
                    default: break;
                }
            }
            return;
        }
        
        // Fall back to stack-based assignment
        auto it = locals.find(id->name);
        
        // OPTIMIZATION: For stack variables with small constant compound assignments
        if (it != locals.end() && !isFloat && valueIsSmall && 
            (node.op == TokenType::PLUS_ASSIGN || node.op == TokenType::MINUS_ASSIGN)) {
            asm_.mov_rax_mem_rbp(it->second);
            if (node.op == TokenType::PLUS_ASSIGN) {
                asm_.code.push_back(0x48); asm_.code.push_back(0x05);
                asm_.code.push_back(constVal & 0xFF);
                asm_.code.push_back((constVal >> 8) & 0xFF);
                asm_.code.push_back((constVal >> 16) & 0xFF);
                asm_.code.push_back((constVal >> 24) & 0xFF);
            } else {
                asm_.code.push_back(0x48); asm_.code.push_back(0x2D);
                asm_.code.push_back(constVal & 0xFF);
                asm_.code.push_back((constVal >> 8) & 0xFF);
                asm_.code.push_back((constVal >> 16) & 0xFF);
                asm_.code.push_back((constVal >> 24) & 0xFF);
            }
            asm_.mov_mem_rbp_rax(it->second);
            return;
        }
        
        node.value->accept(*this);
        
        if (it != locals.end()) {
            if (isFloat && lastExprWasFloat_) {
                // Float assignment
                if (node.op == TokenType::PLUS_ASSIGN) {
                    asm_.movsd_xmm1_mem_rbp(it->second);
                    asm_.addsd_xmm0_xmm1();
                } else if (node.op == TokenType::MINUS_ASSIGN) {
                    asm_.movsd_xmm1_xmm0();
                    asm_.movsd_xmm0_mem_rbp(it->second);
                    asm_.subsd_xmm0_xmm1();
                } else if (node.op == TokenType::STAR_ASSIGN) {
                    asm_.movsd_xmm1_mem_rbp(it->second);
                    asm_.mulsd_xmm0_xmm1();
                } else if (node.op == TokenType::SLASH_ASSIGN) {
                    asm_.movsd_xmm1_xmm0();
                    asm_.movsd_xmm0_mem_rbp(it->second);
                    asm_.divsd_xmm0_xmm1();
                }
                asm_.movsd_mem_rbp_xmm0(it->second);
            } else {
                // Integer assignment
                if (node.op == TokenType::PLUS_ASSIGN) {
                    asm_.mov_rcx_mem_rbp(it->second);
                    asm_.add_rax_rcx();
                } else if (node.op == TokenType::MINUS_ASSIGN) {
                    asm_.mov_rcx_rax();
                    asm_.mov_rax_mem_rbp(it->second);
                    asm_.sub_rax_rcx();
                } else if (node.op == TokenType::STAR_ASSIGN) {
                    asm_.mov_rcx_mem_rbp(it->second);
                    asm_.imul_rax_rcx();
                } else if (node.op == TokenType::SLASH_ASSIGN) {
                    asm_.mov_rcx_rax();  // rcx = divisor
                    asm_.mov_rax_mem_rbp(it->second);  // rax = dividend
                    asm_.cqo();  // sign-extend rax to rdx:rax
                    asm_.idiv_rcx();  // rax = quotient
                }
                asm_.mov_mem_rbp_rax(it->second);
            }
        } else {
            allocLocal(id->name);
            if (isFloat && lastExprWasFloat_) {
                asm_.movsd_mem_rbp_xmm0(locals[id->name]);
            } else {
                asm_.mov_mem_rbp_rax(locals[id->name]);
            }
        }
    } else {
        // Dynamic target (MemberExpr, IndexExpr)
        // Evaluate target to get address
        node.target->accept(*this);
        asm_.push_rax(); // push address
        
        // Evaluate value
        node.value->accept(*this);
        asm_.pop_rcx(); // pop address
        
        // Store: [rcx] = rax
        asm_.mov_mem_rcx_rax();
    }
}

void NativeCodeGen::visit(Block& node) {
    for (auto& stmt : node.statements) {
        // Skip nested function declarations - they're emitted separately after the parent function
        if (dynamic_cast<FnDecl*>(stmt.get())) {
            continue;
        }
        stmt->accept(*this);
    }
}

void NativeCodeGen::visit(IfStmt& node) {
    std::string elseLabel = newLabel("if_else");
    std::string endLabel = newLabel("if_end");
    
    node.condition->accept(*this);
    asm_.test_rax_rax();
    asm_.jz_rel32(elseLabel);
    node.thenBranch->accept(*this);
    
    // Only emit jump to end if the then branch doesn't end with a terminator
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
        
        // Only emit jump if this branch doesn't terminate
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
    
    // Push loop context for break/continue
    loopStack.push_back({loopLabel, endLabel});
    
    // Note: We used to clear constant tracking here, but that was too aggressive
    // and broke constant string lookups. Instead, we should only invalidate
    // constants that are actually modified inside the loop.
    // For now, just clear integer constants since they're more likely to be modified
    constVars.clear();
    // Don't clear constStrVars - string constants are rarely modified in loops
    
    asm_.label(loopLabel);
    node.condition->accept(*this);
    asm_.test_rax_rax();
    asm_.jz_rel32(endLabel);
    node.body->accept(*this);
    
    // Only emit jump back to loop start if body doesn't end with a terminator
    // (continue already jumps to loopLabel, return exits the function)
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
    
    // Push loop context for break/continue
    loopStack.push_back({continueLabel, endLabel});
    
    // For loop variables are always stored on the stack, not in registers
    // Remove any register assignment for the loop variable to avoid mismatch
    varRegisters_.erase(node.var);
    
    // Handle range expression: for i in 1..10 (INCLUSIVE: 1, 2, ..., 10)
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        range->start->accept(*this);
        allocLocal(node.var);
        asm_.mov_mem_rbp_rax(locals[node.var]);
        
        range->end->accept(*this);
        allocLocal("$end");
        asm_.mov_mem_rbp_rax(locals["$end"]);
        
        // Clear any constant tracking for loop var since it changes
        constVars.erase(node.var);
        
        asm_.label(loopLabel);
        asm_.mov_rax_mem_rbp(locals[node.var]);
        asm_.cmp_rax_mem_rbp(locals["$end"]);
        asm_.jg_rel32(endLabel);  // INCLUSIVE: use jg (greater) not jge (greater or equal)
        
        node.body->accept(*this);
        
        asm_.label(continueLabel);
        asm_.mov_rax_mem_rbp(locals[node.var]);
        asm_.inc_rax();
        asm_.mov_mem_rbp_rax(locals[node.var]);
        asm_.jmp_rel32(loopLabel);
        
        asm_.label(endLabel);
        loopStack.pop_back();
        return;
    }
    
    // Handle range() function call: for i in range(1, 11) (EXCLUSIVE like Python)
    // Note: range() is exclusive, but .. operator is inclusive
    if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (auto* calleeId = dynamic_cast<Identifier*>(call->callee.get())) {
            if (calleeId->name == "range" && call->args.size() >= 1) {
                // range(end) or range(start, end) - EXCLUSIVE (Python-style)
                if (call->args.size() == 1) {
                    // range(end) - start from 0, go to end-1
                    asm_.xor_rax_rax();
                    allocLocal(node.var);
                    asm_.mov_mem_rbp_rax(locals[node.var]);
                    
                    call->args[0]->accept(*this);
                    allocLocal("$end");
                    asm_.mov_mem_rbp_rax(locals["$end"]);
                } else {
                    // range(start, end) - go from start to end-1
                    call->args[0]->accept(*this);
                    allocLocal(node.var);
                    asm_.mov_mem_rbp_rax(locals[node.var]);
                    
                    call->args[1]->accept(*this);
                    allocLocal("$end");
                    asm_.mov_mem_rbp_rax(locals["$end"]);
                }
                
                // Clear any constant tracking for loop var since it changes
                constVars.erase(node.var);
                
                asm_.label(loopLabel);
                asm_.mov_rax_mem_rbp(locals[node.var]);
                asm_.cmp_rax_mem_rbp(locals["$end"]);
                asm_.jge_rel32(endLabel);  // EXCLUSIVE: use jge (greater or equal)
                
                node.body->accept(*this);
                
                asm_.label(continueLabel);
                asm_.mov_rax_mem_rbp(locals[node.var]);
                asm_.inc_rax();
                asm_.mov_mem_rbp_rax(locals[node.var]);
                asm_.jmp_rel32(loopLabel);
                
                asm_.label(endLabel);
                loopStack.pop_back();
                return;
            }
        }
    }
    
    // Handle iteration over list variable: for n in numbers
    if (auto* ident = dynamic_cast<Identifier*>(node.iterable.get())) {
        // Check if we know the list size
        auto sizeIt = listSizes.find(ident->name);
        auto constListIt = constListVars.find(ident->name);
        
        if (sizeIt != listSizes.end() && sizeIt->second > 0) {
            size_t listSize = sizeIt->second;
            
            // Get list pointer
            node.iterable->accept(*this);
            allocLocal("$for_list_ptr");
            asm_.mov_mem_rbp_rax(locals["$for_list_ptr"]);
            
            // Initialize index
            allocLocal("$for_idx");
            asm_.xor_rax_rax();
            asm_.mov_mem_rbp_rax(locals["$for_idx"]);
            
            // Store list size
            allocLocal("$for_list_size");
            asm_.mov_rax_imm64((int64_t)listSize);
            asm_.mov_mem_rbp_rax(locals["$for_list_size"]);
            
            // Allocate loop variable
            allocLocal(node.var);
            constVars.erase(node.var);
            
            asm_.label(loopLabel);
            
            // Check if index < size
            asm_.mov_rax_mem_rbp(locals["$for_idx"]);
            asm_.cmp_rax_mem_rbp(locals["$for_list_size"]);
            asm_.jge_rel32(endLabel);
            
            // Load current element: var = list[idx]
            asm_.mov_rcx_mem_rbp(locals["$for_list_ptr"]);
            asm_.mov_rax_mem_rbp(locals["$for_idx"]);
            // rax *= 8
            asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
            asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
            asm_.add_rax_rcx();
            asm_.mov_rax_mem_rax();
            asm_.mov_mem_rbp_rax(locals[node.var]);
            
            // Execute body
            node.body->accept(*this);
            
            asm_.label(continueLabel);
            
            // Increment index
            asm_.mov_rax_mem_rbp(locals["$for_idx"]);
            asm_.inc_rax();
            asm_.mov_mem_rbp_rax(locals["$for_idx"]);
            
            asm_.jmp_rel32(loopLabel);
            
            asm_.label(endLabel);
            loopStack.pop_back();
            return;
        } else if (constListIt != constListVars.end() && !constListIt->second.empty()) {
            // Constant list - unroll the loop
            const auto& values = constListIt->second;
            
            allocLocal(node.var);
            
            for (size_t i = 0; i < values.size(); i++) {
                // Set loop variable to constant value
                asm_.mov_rax_imm64(values[i]);
                asm_.mov_mem_rbp_rax(locals[node.var]);
                constVars[node.var] = values[i];
                
                // Execute body
                node.body->accept(*this);
            }
            
            asm_.label(continueLabel);
            asm_.label(endLabel);
            loopStack.pop_back();
            return;
        }
        
        // Unknown list - skip the loop
        asm_.label(loopLabel);
        asm_.label(continueLabel);
        asm_.label(endLabel);
    }
    
    loopStack.pop_back();
}

void NativeCodeGen::visit(MatchStmt& node) {
    // Generate code for match statement
    // Evaluate the value to match
    node.value->accept(*this);
    
    // Save the value in a temporary location
    allocLocal("$match_value");
    asm_.mov_mem_rbp_rax(locals["$match_value"]);
    
    std::string endLabel = newLabel("match_end");
    
    // Generate code for each case
    for (size_t i = 0; i < node.cases.size(); i++) {
        auto& [pattern, body] = node.cases[i];
        
        // Check if this is the wildcard pattern '_'
        if (auto* ident = dynamic_cast<Identifier*>(pattern.get())) {
            if (ident->name == "_") {
                // Wildcard matches everything - just execute the body
                body->accept(*this);
                asm_.jmp_rel32(endLabel);
                continue;
            }
            
            // Check if this is a variable binding pattern (lowercase identifier that's not a known constant)
            // Variable binding: bind the match value to this name
            if (ident->name.length() > 0 && std::islower(ident->name[0]) && 
                constVars.find(ident->name) == constVars.end() &&
                constStrVars.find(ident->name) == constStrVars.end()) {
                // This is a variable binding - bind match value to this variable
                allocLocal(ident->name);
                asm_.mov_rax_mem_rbp(locals["$match_value"]);
                asm_.mov_mem_rbp_rax(locals[ident->name]);
                
                // Execute the body with the bound variable
                body->accept(*this);
                asm_.jmp_rel32(endLabel);
                continue;
            }
        }
        
        // Check for tuple/list destructuring pattern
        if (auto* listExpr = dynamic_cast<ListExpr*>(pattern.get())) {
            std::string nextCase = newLabel("match_case");
            
            // For each element in the pattern, check if it matches
            // and bind variables
            bool hasVariables = false;
            std::vector<std::pair<size_t, std::string>> bindings;
            
            for (size_t j = 0; j < listExpr->elements.size(); j++) {
                auto* elem = listExpr->elements[j].get();
                if (auto* elemIdent = dynamic_cast<Identifier*>(elem)) {
                    if (elemIdent->name != "_" && std::islower(elemIdent->name[0])) {
                        hasVariables = true;
                        bindings.push_back({j, elemIdent->name});
                    }
                }
            }
            
            // Load match value (should be a list pointer)
            asm_.mov_rax_mem_rbp(locals["$match_value"]);
            
            // Bind each variable to the corresponding list element
            for (auto& [idx, varName] : bindings) {
                allocLocal(varName);
                asm_.mov_rax_mem_rbp(locals["$match_value"]);
                if (idx > 0) {
                    asm_.add_rax_imm32((int32_t)(idx * 8));
                }
                asm_.mov_rax_mem_rax();  // Load element value
                asm_.mov_mem_rbp_rax(locals[varName]);
            }
            
            // Execute the body
            body->accept(*this);
            asm_.jmp_rel32(endLabel);
            continue;
        }
        
        // Check for record destructuring pattern
        if (auto* recordExpr = dynamic_cast<RecordExpr*>(pattern.get())) {
            // For records, we'd need field offset information
            // For now, just bind the whole value and execute body
            body->accept(*this);
            asm_.jmp_rel32(endLabel);
            continue;
        }
        
        std::string nextCase = newLabel("match_case");
        
        // Load the match value
        asm_.mov_rax_mem_rbp(locals["$match_value"]);
        asm_.push_rax();
        
        // Evaluate the pattern (literal comparison)
        pattern->accept(*this);
        asm_.pop_rcx();
        
        // Compare: if not equal, jump to next case
        asm_.cmp_rax_rcx();
        asm_.jnz_rel32(nextCase);
        
        // Match! Execute the body
        body->accept(*this);
        asm_.jmp_rel32(endLabel);
        
        asm_.label(nextCase);
    }
    
    // Default case (if any)
    if (node.defaultCase) {
        node.defaultCase->accept(*this);
    }
    
    asm_.label(endLabel);
}

void NativeCodeGen::visit(ReturnStmt& node) {
    if (node.value) {
        node.value->accept(*this);
    } else {
        asm_.xor_rax_rax();
    }
    
    if (stackAllocated_) {
        // Standard epilogue
        // 1. Deallocate stack space
        asm_.add_rsp_imm32(functionStackSize_);
        
        // 2. Restore callee-saved registers (includes RDI)
        emitRestoreCalleeSavedRegs();
        
        // 3. Restore rbp
        asm_.pop_rbp();
    } else {
        // Minimal epilogue for leaf functions
        emitRestoreCalleeSavedRegs();
    }
    
    asm_.ret();
}

void NativeCodeGen::visit(BreakStmt& node) {
    (void)node;
    if (!loopStack.empty()) {
        asm_.jmp_rel32(loopStack.back().breakLabel);
    }
}

void NativeCodeGen::visit(ContinueStmt& node) {
    (void)node;
    if (!loopStack.empty()) {
        asm_.jmp_rel32(loopStack.back().continueLabel);
    }
}

void NativeCodeGen::visit(TryStmt& node) {
    // Try/else: evaluate tryExpr, if it returns 0/nil/false, evaluate elseExpr
    // This is a simple "nil-coalescing" pattern: try expr else default
    
    std::string elseLabel = newLabel("try_else");
    std::string endLabel = newLabel("try_end");
    
    // Evaluate the try expression
    node.tryExpr->accept(*this);
    
    // Check if result is 0/nil/false
    asm_.test_rax_rax();
    asm_.jz_rel32(elseLabel);
    
    // Try succeeded, jump to end
    asm_.jmp_rel32(endLabel);
    
    // Else branch
    asm_.label(elseLabel);
    if (node.elseExpr) {
        node.elseExpr->accept(*this);
    } else {
        asm_.xor_rax_rax();  // Default to 0 if no else
    }
    
    asm_.label(endLabel);
}

void NativeCodeGen::visit(DeleteStmt& node) {
    node.expr->accept(*this);
    asm_.push_rax();
    
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    asm_.mov_rdx_rax();
    asm_.pop_r8();
    
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("HeapFree"));
    asm_.add_rsp_imm32(0x28);
}

} // namespace flex
