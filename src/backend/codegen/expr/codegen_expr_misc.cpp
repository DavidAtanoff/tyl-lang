// Tyl Compiler - Native Code Generator Miscellaneous Expressions
// Note: AssignExpr moved to codegen_expr_assign.cpp
// Note: LambdaExpr moved to codegen_expr_lambda.cpp
// Note: AddressOfExpr, DerefExpr, NewExpr, CastExpr moved to codegen_expr_pointer.cpp
// Note: AwaitExpr, SpawnExpr, PropagateExpr, DSLBlock moved to codegen_expr_async.cpp

#include "backend/codegen/codegen_base.h"

namespace tyl {

// All visitor methods have been moved to modular files:
// - codegen_expr_assign.cpp: AssignExpr
// - codegen_expr_lambda.cpp: LambdaExpr
// - codegen_expr_pointer.cpp: AddressOfExpr, DerefExpr, NewExpr, CastExpr
// - codegen_expr_async.cpp: AwaitExpr, SpawnExpr, PropagateExpr, DSLBlock

} // namespace tyl
