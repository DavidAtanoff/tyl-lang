// Tyl Compiler - Native Code Generator Call Builtins
// Handles: len, upper, contains, push, pop, range, platform, arch, str

#include "backend/codegen/codegen_base.h"

namespace tyl {

// Helper function to handle builtin calls - returns true if handled
bool handleBuiltinCall(NativeCodeGen* gen, CallExpr& node, Identifier* id);

// This file contains the builtin function implementations
// The main visit(CallExpr) is in codegen_call_core.cpp

} // namespace tyl
