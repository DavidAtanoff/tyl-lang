// Tyl Compiler - Native Code Generator Type Conversion Builtin Calls
// Handles: int, float, str, bool, type conversions

#include "backend/codegen/codegen_base.h"

namespace tyl {

// Type conversion builtin implementations extracted from codegen_call_core.cpp

void NativeCodeGen::emitConvInt(CallExpr& node) {
    // int() - convert to integer
    int64_t intVal;
    if (tryEvalConstant(node.args[0].get(), intVal)) {
        asm_.mov_rax_imm64(intVal);
        lastExprWasFloat_ = false;
        return;
    }
    
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        asm_.mov_rax_imm64(static_cast<int64_t>(floatVal));
        lastExprWasFloat_ = false;
        return;
    }
    
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        // Parse string to int
        int64_t result = 0;
        bool negative = false;
        size_t i = 0;
        while (i < strVal.size() && (strVal[i] == ' ' || strVal[i] == '\t')) i++;
        if (i < strVal.size() && strVal[i] == '-') { negative = true; i++; }
        else if (i < strVal.size() && strVal[i] == '+') { i++; }
        while (i < strVal.size() && strVal[i] >= '0' && strVal[i] <= '9') {
            result = result * 10 + (strVal[i] - '0');
            i++;
        }
        if (negative) result = -result;
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    // Runtime conversion
    node.args[0]->accept(*this);
    
    if (lastExprWasFloat_) {
        // Convert float to int
        asm_.cvttsd2si_rax_xmm0();
    }
    // If already int, value is in rax
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitConvFloat(CallExpr& node) {
    // float() - convert to float
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        uint32_t rva = addFloatConstant(floatVal);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Try string-to-float conversion at compile time
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        try {
            double result = std::stod(strVal);
            uint32_t rva = addFloatConstant(result);
            asm_.movsd_xmm0_mem_rip(rva);
            lastExprWasFloat_ = true;
            return;
        } catch (...) {
            // Fall through to runtime
        }
    }
    
    int64_t intVal;
    if (tryEvalConstant(node.args[0].get(), intVal)) {
        double result = static_cast<double>(intVal);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Runtime conversion
    node.args[0]->accept(*this);
    
    if (!lastExprWasFloat_) {
        // Convert int to float
        asm_.cvtsi2sd_xmm0_rax();
    }
    // If already float, value is in xmm0
    lastExprWasFloat_ = true;
}

void NativeCodeGen::emitConvStr(CallExpr& node) {
    // str() - convert to string
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        lastExprWasFloat_ = false;
        return;
    }
    
    int64_t intVal;
    if (tryEvalConstant(node.args[0].get(), intVal)) {
        std::string result = std::to_string(intVal);
        uint32_t rva = addString(result);
        asm_.lea_rax_rip_fixup(rva);
        lastExprWasFloat_ = false;
        return;
    }
    
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        // Format float to string
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", floatVal);
        uint32_t rva = addString(buf);
        asm_.lea_rax_rip_fixup(rva);
        lastExprWasFloat_ = false;
        return;
    }
    
    // Runtime conversion - use inline itoa/ftoa (same as print)
    node.args[0]->accept(*this);
    
    if (lastExprWasFloat_) {
        // Float to string - use ftoa which returns pointer in rax
        emitFtoaCall();
    } else {
        // Int to string - use itoa which returns pointer in rax, length in rcx
        emitItoaCall();
    }
    
    // Result is already in rax (pointer to string in static buffer)
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitConvBool(CallExpr& node) {
    // bool() - convert to boolean
    int64_t intVal;
    if (tryEvalConstant(node.args[0].get(), intVal)) {
        asm_.mov_rax_imm64(intVal != 0 ? 1 : 0);
        lastExprWasFloat_ = false;
        return;
    }
    
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        bool result = !strVal.empty() && strVal != "0" && 
                      strVal != "false" && strVal != "False" && strVal != "FALSE";
        asm_.mov_rax_imm64(result ? 1 : 0);
        lastExprWasFloat_ = false;
        return;
    }
    
    // Runtime conversion
    node.args[0]->accept(*this);
    
    if (lastExprWasFloat_) {
        // Compare float to 0.0
        asm_.xorpd_xmm1_xmm1();
        asm_.ucomisd_xmm0_xmm1();
        asm_.setne_al();
        asm_.movzx_rax_al();
    } else {
        // Compare int to 0
        asm_.test_rax_rax();
        asm_.setne_al();
        asm_.movzx_rax_al();
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitConvType(CallExpr& node) {
    // type() - get type name as string
    auto& arg = node.args[0];
    
    if (dynamic_cast<IntegerLiteral*>(arg.get())) {
        uint32_t rva = addString("int");
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    if (dynamic_cast<FloatLiteral*>(arg.get())) {
        uint32_t rva = addString("float");
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    if (dynamic_cast<StringLiteral*>(arg.get()) || dynamic_cast<InterpolatedString*>(arg.get())) {
        uint32_t rva = addString("str");
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    if (dynamic_cast<BoolLiteral*>(arg.get())) {
        uint32_t rva = addString("bool");
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    if (dynamic_cast<ListExpr*>(arg.get())) {
        uint32_t rva = addString("list");
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    if (dynamic_cast<RecordExpr*>(arg.get())) {
        uint32_t rva = addString("record");
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    if (dynamic_cast<MapExpr*>(arg.get())) {
        uint32_t rva = addString("map");
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    if (auto* ident = dynamic_cast<Identifier*>(arg.get())) {
        if (constVars.count(ident->name)) {
            uint32_t rva = addString("int");
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
        if (constFloatVars.count(ident->name) || floatVars.count(ident->name)) {
            uint32_t rva = addString("float");
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
        if (constStrVars.count(ident->name)) {
            uint32_t rva = addString("str");
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
        if (listSizes.count(ident->name) || constListVars.count(ident->name)) {
            uint32_t rva = addString("list");
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
        if (varRecordTypes_.count(ident->name)) {
            uint32_t rva = addString(varRecordTypes_[ident->name]);
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
    }
    
    // Default to "unknown"
    uint32_t rva = addString("unknown");
    asm_.lea_rax_rip_fixup(rva);
}

} // namespace tyl
