// Tyl Compiler - Extended Time Builtins for Native Code Generation
// Additional time functions ported from stdlib/time/time.cpp

#include "backend/codegen/codegen_base.h"

namespace tyl {

// now_us() -> int - Current Unix timestamp in microseconds
void NativeCodeGen::emitTimeNowUs(CallExpr& node) {
    (void)node;
    allocLocal("$filetime_us");
    allocLocal("$filetime_us_high");
    
    asm_.lea_rcx_rbp(locals["$filetime_us"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetSystemTimeAsFileTime"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.mov_rax_mem_rbp(locals["$filetime_us"]);
    asm_.mov_rcx_imm64(116444736000000000LL);
    asm_.sub_rax_rcx();
    asm_.mov_rcx_imm64(10);  // Convert to microseconds
    asm_.cqo();
    asm_.idiv_rcx();
}

// weekday(timestamp?) -> int - Get day of week (0=Sunday, 6=Saturday)
void NativeCodeGen::emitTimeWeekday(CallExpr& node) {
    (void)node;
    emitGetLocalTimeField(4);  // wDayOfWeek is at offset 4
}

// day_of_year(timestamp?) -> int - Get day of year (1-366)
void NativeCodeGen::emitTimeDayOfYear(CallExpr& node) {
    (void)node;
    // SYSTEMTIME doesn't have day of year, compute from month/day
    // Simplified - just return day for now
    emitGetLocalTimeField(6);
}

// make_time(year, month, day, hour?, min?, sec?) -> int - Create timestamp
void NativeCodeGen::emitTimeMakeTime(CallExpr& node) {
    // Simplified - return 0 for now
    // Full implementation would use SystemTimeToFileTime
    (void)node;
    asm_.xor_rax_rax();
}

// add_days(timestamp, days) -> int - Add days to timestamp
void NativeCodeGen::emitTimeAddDays(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    
    // days * 86400 seconds
    asm_.mov_rcx_imm64(86400);
    asm_.imul_rax_rcx();
    
    asm_.pop_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8); // add rax, rcx
}

// add_hours(timestamp, hours) -> int - Add hours to timestamp
void NativeCodeGen::emitTimeAddHours(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    
    // hours * 3600 seconds
    asm_.mov_rcx_imm64(3600);
    asm_.imul_rax_rcx();
    
    asm_.pop_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8);
}

// diff_days(timestamp1, timestamp2) -> int - Difference in days
void NativeCodeGen::emitTimeDiffDays(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    
    // (t2 - t1) / 86400
    // sub rcx, rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x29); asm_.code.push_back(0xC1);
    asm_.mov_rax_rcx();
    asm_.mov_rcx_imm64(86400);
    asm_.cqo();
    asm_.idiv_rcx();
}

// is_leap_year(year) -> bool - Check if year is a leap year
void NativeCodeGen::emitTimeIsLeapYear(CallExpr& node) {
    int64_t year;
    if (tryEvalConstant(node.args[0].get(), year)) {
        bool result = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        asm_.mov_rax_imm64(result ? 1 : 0);
        return;
    }
    
    // Runtime
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    // Check year % 400 == 0
    asm_.mov_rax_rcx();
    asm_.mov_rdx_imm64(400);
    asm_.cqo();
    // idiv rcx (use rcx as divisor instead of rdx)
    // mov r8, rax (save year to r8)
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC0);
    asm_.mov_rax_rcx();  // year to rax
    asm_.mov_rcx_imm64(400);
    asm_.cqo();
    asm_.idiv_rcx();
    // test rdx, rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0x85); asm_.code.push_back(0xD2);
    
    std::string trueLabel = newLabel("leap_true");
    std::string check100 = newLabel("leap_check100");
    std::string falseLabel = newLabel("leap_false");
    std::string doneLabel = newLabel("leap_done");
    
    asm_.jz_rel32(trueLabel);
    
    // Check year % 100 == 0
    // mov rax, r8 (restore year)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xC0);
    asm_.mov_rcx_imm64(100);
    asm_.cqo();
    asm_.idiv_rcx();
    // test rdx, rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0x85); asm_.code.push_back(0xD2);
    asm_.jz_rel32(falseLabel);
    
    // Check year % 4 == 0
    // mov rax, r8 (restore year)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xC0);
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xE0); asm_.code.push_back(0x03); // and rax, 3
    asm_.test_rax_rax();
    asm_.jnz_rel32(falseLabel);
    
    asm_.label(trueLabel);
    asm_.mov_rax_imm64(1);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(falseLabel);
    asm_.xor_rax_rax();
    
    asm_.label(doneLabel);
}

} // namespace tyl
