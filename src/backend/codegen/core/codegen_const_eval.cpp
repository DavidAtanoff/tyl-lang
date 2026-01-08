// Tyl Compiler - Native Code Generator Constant Evaluation
// Handles: tryEvalConstant, tryEvalConstantFloat, tryEvalConstantString

#include "backend/codegen/codegen_base.h"
#include "semantic/ctfe/ctfe_interpreter.h"
#include <cmath>
#include <sstream>

namespace tyl {

bool NativeCodeGen::tryEvalConstant(Expression* expr, int64_t& outValue) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        outValue = intLit->value;
        return true;
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        outValue = boolLit->value ? 1 : 0;
        return true;
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = constVars.find(ident->name);
        if (it != constVars.end()) {
            outValue = it->second;
            return true;
        }
        return false;
    }
    // Handle constant list indexing with constant index (1-based indexing)
    if (auto* indexExpr = dynamic_cast<IndexExpr*>(expr)) {
        if (auto* ident = dynamic_cast<Identifier*>(indexExpr->object.get())) {
            auto constListIt = constListVars.find(ident->name);
            if (constListIt != constListVars.end()) {
                int64_t indexVal;
                if (tryEvalConstant(indexExpr->index.get(), indexVal)) {
                    // Convert 1-based index to 0-based for internal access
                    int64_t zeroBasedIndex = indexVal - 1;
                    if (zeroBasedIndex >= 0 && (size_t)zeroBasedIndex < constListIt->second.size()) {
                        outValue = constListIt->second[zeroBasedIndex];
                        return true;
                    }
                }
            }
        }
        return false;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        int64_t left, right;
        if (tryEvalConstant(binary->left.get(), left) && tryEvalConstant(binary->right.get(), right)) {
            switch (binary->op) {
                case TokenType::PLUS: outValue = left + right; return true;
                case TokenType::MINUS: outValue = left - right; return true;
                case TokenType::STAR: outValue = left * right; return true;
                case TokenType::SLASH: if (right != 0) { outValue = left / right; return true; } break;
                case TokenType::PERCENT: if (right != 0) { outValue = left % right; return true; } break;
                case TokenType::LT: outValue = left < right ? 1 : 0; return true;
                case TokenType::GT: outValue = left > right ? 1 : 0; return true;
                case TokenType::LE: outValue = left <= right ? 1 : 0; return true;
                case TokenType::GE: outValue = left >= right ? 1 : 0; return true;
                case TokenType::EQ: outValue = left == right ? 1 : 0; return true;
                case TokenType::NE: outValue = left != right ? 1 : 0; return true;
                default: break;
            }
        }
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        int64_t val;
        if (tryEvalConstant(unary->operand.get(), val)) {
            switch (unary->op) {
                case TokenType::MINUS: outValue = -val; return true;
                case TokenType::NOT: outValue = !val ? 1 : 0; return true;
                default: break;
            }
        }
    }
    // Handle int() conversion at compile time
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "int" && call->args.size() == 1) {
                int64_t intVal;
                if (tryEvalConstant(call->args[0].get(), intVal)) {
                    outValue = intVal;
                    return true;
                }
                std::string strVal;
                if (tryEvalConstantString(call->args[0].get(), strVal)) {
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
                    outValue = result;
                    return true;
                }
                double floatVal;
                if (tryEvalConstantFloat(call->args[0].get(), floatVal)) {
                    outValue = (int64_t)floatVal;
                    return true;
                }
            }
            if (id->name == "bool" && call->args.size() == 1) {
                int64_t intVal;
                if (tryEvalConstant(call->args[0].get(), intVal)) {
                    outValue = intVal != 0 ? 1 : 0;
                    return true;
                }
                std::string strVal;
                if (tryEvalConstantString(call->args[0].get(), strVal)) {
                    bool result = !strVal.empty() && strVal != "0" && strVal != "false" && strVal != "False" && strVal != "FALSE";
                    outValue = result ? 1 : 0;
                    return true;
                }
            }
            // Handle sizeof(T) at compile time
            if (id->name == "sizeof" && call->args.size() == 1) {
                if (auto* typeIdent = dynamic_cast<Identifier*>(call->args[0].get())) {
                    std::string typeName = typeIdent->name;
                    outValue = getTypeSize(typeName);
                    return true;
                }
            }
            // Handle alignof(T) at compile time
            if (id->name == "alignof" && call->args.size() == 1) {
                if (auto* typeIdent = dynamic_cast<Identifier*>(call->args[0].get())) {
                    std::string typeName = typeIdent->name;
                    outValue = getTypeAlignment(typeName);
                    return true;
                }
            }
            // Handle offsetof(Record, field) at compile time
            if (id->name == "offsetof" && call->args.size() == 2) {
                if (auto* recordIdent = dynamic_cast<Identifier*>(call->args[0].get())) {
                    if (auto* fieldIdent = dynamic_cast<Identifier*>(call->args[1].get())) {
                        std::string recordName = recordIdent->name;
                        std::string fieldName = fieldIdent->name;
                        int64_t offset = 0;
                        
                        auto it = recordTypes_.find(recordName);
                        if (it != recordTypes_.end()) {
                            for (size_t i = 0; i < it->second.fieldNames.size(); i++) {
                                if (it->second.fieldNames[i] == fieldName) {
                                    offset = getRecordFieldOffset(recordName, static_cast<int>(i));
                                    offset -= 8;  // Subtract header
                                    break;
                                }
                            }
                        }
                        outValue = offset;
                        return true;
                    }
                }
            }
            
            // Handle comptime function calls via CTFE interpreter
            if (ctfe_.isComptimeFunction(id->name)) {
                std::vector<CTFEInterpValue> args;
                bool allArgsConst = true;
                
                for (auto& arg : call->args) {
                    auto val = ctfe_.evaluateExpr(arg.get());
                    if (val) {
                        args.push_back(*val);
                    } else {
                        allArgsConst = false;
                        break;
                    }
                }
                
                if (allArgsConst) {
                    try {
                        auto result = ctfe_.evaluateCall(id->name, args);
                        if (result) {
                            auto intVal = CTFEInterpreter::toInt(*result);
                            if (intVal) {
                                outValue = *intVal;
                                return true;
                            }
                        }
                    } catch (const CTFEInterpError& e) {
                        // CTFE evaluation failed - fall through
                        (void)e;
                    }
                }
            }
        }
    }
    return false;
}

bool NativeCodeGen::tryEvalConstantFloat(Expression* expr, double& outValue) {
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        outValue = floatLit->value;
        return true;
    }
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        outValue = static_cast<double>(intLit->value);
        return true;
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = constFloatVars.find(ident->name);
        if (it != constFloatVars.end()) {
            outValue = it->second;
            return true;
        }
        auto intIt = constVars.find(ident->name);
        if (intIt != constVars.end()) {
            outValue = static_cast<double>(intIt->second);
            return true;
        }
        return false;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        double left, right;
        if (tryEvalConstantFloat(binary->left.get(), left) && tryEvalConstantFloat(binary->right.get(), right)) {
            switch (binary->op) {
                case TokenType::PLUS: outValue = left + right; return true;
                case TokenType::MINUS: outValue = left - right; return true;
                case TokenType::STAR: outValue = left * right; return true;
                case TokenType::SLASH: if (right != 0.0) { outValue = left / right; return true; } break;
                default: break;
            }
        }
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        double val;
        if (tryEvalConstantFloat(unary->operand.get(), val)) {
            switch (unary->op) {
                case TokenType::MINUS: outValue = -val; return true;
                default: break;
            }
        }
    }
    return false;
}

bool NativeCodeGen::tryEvalConstantString(Expression* expr, std::string& outValue) {
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        outValue = strLit->value;
        return true;
    }
    if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        std::string result;
        for (auto& part : interp->parts) {
            if (auto* str = std::get_if<std::string>(&part)) {
                result += *str;
            } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
                std::string strVal;
                int64_t intVal;
                if (tryEvalConstantString(exprPtr->get(), strVal)) {
                    result += strVal;
                } else if (tryEvalConstant(exprPtr->get(), intVal)) {
                    result += std::to_string(intVal);
                } else {
                    return false;
                }
            }
        }
        outValue = result;
        return true;
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = constStrVars.find(ident->name);
        if (it != constStrVars.end() && !it->second.empty()) {
            outValue = it->second;
            return true;
        }
        return false;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        if (binary->op == TokenType::PLUS) {
            std::string left, right;
            if (tryEvalConstantString(binary->left.get(), left) && tryEvalConstantString(binary->right.get(), right)) {
                outValue = left + right;
                return true;
            }
            int64_t intVal;
            if (dynamic_cast<StringLiteral*>(binary->left.get()) || 
                (dynamic_cast<Identifier*>(binary->left.get()) && 
                 constStrVars.count(dynamic_cast<Identifier*>(binary->left.get())->name))) {
                if (tryEvalConstantString(binary->left.get(), left) && tryEvalConstant(binary->right.get(), intVal)) {
                    outValue = left + std::to_string(intVal);
                    return true;
                }
            }
            if (dynamic_cast<StringLiteral*>(binary->right.get()) || 
                (dynamic_cast<Identifier*>(binary->right.get()) && 
                 constStrVars.count(dynamic_cast<Identifier*>(binary->right.get())->name))) {
                if (tryEvalConstant(binary->left.get(), intVal) && tryEvalConstantString(binary->right.get(), right)) {
                    outValue = std::to_string(intVal) + right;
                    return true;
                }
            }
        }
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "str" && call->args.size() == 1) {
                int64_t intVal;
                if (tryEvalConstant(call->args[0].get(), intVal)) {
                    outValue = std::to_string(intVal);
                    return true;
                }
                std::string strVal;
                if (tryEvalConstantString(call->args[0].get(), strVal)) {
                    outValue = strVal;
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace tyl
