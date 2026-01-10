// Tyl Compiler - AST Printer Implementation

#include "ast_printer.h"
#include <iostream>

namespace tyl {

void ASTPrinter::print(const std::string& s) {
    std::cout << std::string(indent * 2, ' ') << s << "\n";
}

void ASTPrinter::visit(IntegerLiteral& n) { print("Int: " + std::to_string(n.value)); }
void ASTPrinter::visit(FloatLiteral& n) { print("Float: " + std::to_string(n.value)); }
void ASTPrinter::visit(StringLiteral& n) { print("String: \"" + n.value + "\""); }
void ASTPrinter::visit(CharLiteral& n) { print("Char: " + std::to_string(n.value)); }
void ASTPrinter::visit(ByteStringLiteral& n) { 
    std::string hex;
    for (uint8_t b : n.value) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", b);
        hex += buf;
    }
    print("ByteString: " + hex + (n.isRaw ? " (raw)" : "")); 
}
void ASTPrinter::visit(InterpolatedString& n) { 
    print("InterpolatedString");
    indent++;
    for (auto& part : n.parts) {
        if (auto* str = std::get_if<std::string>(&part)) print("Part: \"" + *str + "\"");
        else if (auto* expr = std::get_if<ExprPtr>(&part)) (*expr)->accept(*this);
    }
    indent--;
}
void ASTPrinter::visit(BoolLiteral& n) { print("Bool: " + std::string(n.value ? "true" : "false")); }
void ASTPrinter::visit(NilLiteral&) { print("Nil"); }
void ASTPrinter::visit(Identifier& n) { print("Identifier: " + n.name); }

void ASTPrinter::visit(BinaryExpr& n) {
    print("BinaryExpr: " + tokenTypeToString(n.op));
    indent++; n.left->accept(*this); n.right->accept(*this); indent--;
}

void ASTPrinter::visit(UnaryExpr& n) {
    print("UnaryExpr: " + tokenTypeToString(n.op));
    indent++; n.operand->accept(*this); indent--;
}

void ASTPrinter::visit(CallExpr& n) {
    print("CallExpr");
    indent++; n.callee->accept(*this);
    for (auto& arg : n.args) arg->accept(*this);
    indent--;
}

void ASTPrinter::visit(MemberExpr& n) {
    print("MemberExpr: ." + n.member);
    indent++; n.object->accept(*this); indent--;
}

void ASTPrinter::visit(IndexExpr& n) {
    print("IndexExpr");
    indent++; n.object->accept(*this); n.index->accept(*this); indent--;
}

void ASTPrinter::visit(ListExpr& n) {
    print("ListExpr");
    indent++; for (auto& e : n.elements) e->accept(*this); indent--;
}


void ASTPrinter::visit(RecordExpr& n) {
    print("RecordExpr" + (n.typeName.empty() ? "" : ": " + n.typeName));
    indent++;
    for (auto& [name, val] : n.fields) {
        print(name + ":");
        indent++; val->accept(*this); indent--;
    }
    indent--;
}

void ASTPrinter::visit(MapExpr& n) {
    print("MapExpr");
    indent++;
    for (auto& [key, val] : n.entries) {
        print("Entry:");
        indent++;
        key->accept(*this);
        val->accept(*this);
        indent--;
    }
    indent--;
}

void ASTPrinter::visit(RangeExpr& n) {
    print("RangeExpr");
    indent++; n.start->accept(*this); n.end->accept(*this);
    if (n.step) n.step->accept(*this);
    indent--;
}

void ASTPrinter::visit(LambdaExpr& n) {
    std::string params;
    for (size_t i = 0; i < n.params.size(); i++) {
        if (i > 0) params += ", ";
        params += n.params[i].first;
        if (!n.params[i].second.empty()) {
            params += ": " + n.params[i].second;
        }
    }
    print("LambdaExpr(" + params + ")");
    indent++; n.body->accept(*this); indent--;
}

void ASTPrinter::visit(TernaryExpr& n) {
    print("TernaryExpr");
    indent++; n.condition->accept(*this); n.thenExpr->accept(*this); n.elseExpr->accept(*this); indent--;
}

void ASTPrinter::visit(ListCompExpr& n) {
    print("ListCompExpr: " + n.var);
    indent++; n.expr->accept(*this); n.iterable->accept(*this);
    if (n.condition) n.condition->accept(*this);
    indent--;
}

void ASTPrinter::visit(AddressOfExpr& n) { print("AddressOf"); indent++; n.operand->accept(*this); indent--; }
void ASTPrinter::visit(BorrowExpr& n) { print(n.isMutable ? "BorrowMut" : "Borrow"); indent++; n.operand->accept(*this); indent--; }
void ASTPrinter::visit(DerefExpr& n) { print("Deref"); indent++; n.operand->accept(*this); indent--; }
void ASTPrinter::visit(NewExpr& n) {
    print("New: " + n.typeName);
    indent++; for (auto& arg : n.args) arg->accept(*this); indent--;
}
void ASTPrinter::visit(CastExpr& n) { print("Cast: " + n.targetType); indent++; n.expr->accept(*this); indent--; }
void ASTPrinter::visit(AwaitExpr& n) { print("Await"); indent++; n.operand->accept(*this); indent--; }
void ASTPrinter::visit(SpawnExpr& n) { print("Spawn"); indent++; n.operand->accept(*this); indent--; }
void ASTPrinter::visit(DSLBlock& n) {
    print("DSLBlock: " + n.dslName);
    indent++; print("Content: " + n.rawContent.substr(0, 50) + (n.rawContent.length() > 50 ? "..." : "")); indent--;
}

void ASTPrinter::visit(ExprStmt& n) { print("ExprStmt"); indent++; n.expr->accept(*this); indent--; }

void ASTPrinter::visit(VarDecl& n) {
    std::string mod = n.isConst ? "const " : (n.isMutable ? "var " : "let ");
    print("VarDecl: " + mod + n.name + (n.typeName.empty() ? "" : ": " + n.typeName));
    if (n.initializer) { indent++; n.initializer->accept(*this); indent--; }
}

void ASTPrinter::visit(DestructuringDecl& n) {
    std::string kind = n.kind == DestructuringDecl::Kind::TUPLE ? "tuple" : "record";
    std::string names;
    for (size_t i = 0; i < n.names.size(); i++) {
        if (i > 0) names += ", ";
        names += n.names[i];
    }
    print("DestructuringDecl: " + kind + " (" + names + ")");
    if (n.initializer) { indent++; n.initializer->accept(*this); indent--; }
}

void ASTPrinter::visit(AssignStmt& n) {
    print("AssignStmt: " + tokenTypeToString(n.op));
    indent++; n.target->accept(*this); n.value->accept(*this); indent--;
}

void ASTPrinter::visit(Block& n) {
    print("Block");
    indent++; for (auto& s : n.statements) s->accept(*this); indent--;
}

void ASTPrinter::visit(IfStmt& n) {
    print("IfStmt");
    indent++; n.condition->accept(*this); n.thenBranch->accept(*this);
    if (n.elseBranch) n.elseBranch->accept(*this);
    indent--;
}

void ASTPrinter::visit(WhileStmt& n) {
    print("WhileStmt");
    indent++; n.condition->accept(*this); n.body->accept(*this); indent--;
}

void ASTPrinter::visit(ForStmt& n) {
    std::string info = "ForStmt: " + n.var;
    if (!n.label.empty()) info += " [label: " + n.label + "]";
    print(info);
    indent++; n.iterable->accept(*this); n.body->accept(*this); indent--;
}

void ASTPrinter::visit(MatchStmt& n) {
    print("MatchStmt");
    indent++; n.value->accept(*this);
    for (auto& case_ : n.cases) { 
        case_.pattern->accept(*this); 
        if (case_.guard) {
            print("Guard:");
            indent++; case_.guard->accept(*this); indent--;
        }
        case_.body->accept(*this); 
    }
    indent--;
}

void ASTPrinter::visit(ReturnStmt& n) {
    print("ReturnStmt");
    if (n.value) { indent++; n.value->accept(*this); indent--; }
}

void ASTPrinter::visit(BreakStmt& n) { 
    std::string info = "BreakStmt";
    if (!n.label.empty()) info += " [label: " + n.label + "]";
    print(info);
}
void ASTPrinter::visit(ContinueStmt& n) { 
    std::string info = "ContinueStmt";
    if (!n.label.empty()) info += " [label: " + n.label + "]";
    print(info);
}

void ASTPrinter::visit(TryStmt& n) {
    print("TryStmt");
    indent++; n.tryExpr->accept(*this); n.elseExpr->accept(*this); indent--;
}

void ASTPrinter::visit(FnDecl& n) {
    std::string typeParams;
    if (!n.typeParams.empty()) {
        typeParams = "[";
        for (size_t i = 0; i < n.typeParams.size(); i++) {
            if (i > 0) typeParams += ", ";
            typeParams += n.typeParams[i];
        }
        typeParams += "]";
    }
    std::string params;
    for (auto& [name, type] : n.params) {
        if (!params.empty()) params += ", ";
        params += name;
        if (!type.empty()) params += ": " + type;
    }
    std::string prefix = n.isComptime ? "comptime " : "";
    print(prefix + "FnDecl: " + n.name + typeParams + "(" + params + ")" + 
          (n.returnType.empty() ? "" : " -> " + n.returnType));
    indent++; if (n.body) n.body->accept(*this); indent--;
}

void ASTPrinter::visit(RecordDecl& n) {
    std::string typeParams;
    if (!n.typeParams.empty()) {
        typeParams = "[";
        for (size_t i = 0; i < n.typeParams.size(); i++) {
            if (i > 0) typeParams += ", ";
            typeParams += n.typeParams[i];
        }
        typeParams += "]";
    }
    print("RecordDecl: " + n.name + typeParams);
    indent++; for (auto& [name, type] : n.fields) print(name + ": " + type); indent--;
}

void ASTPrinter::visit(UnionDecl& n) {
    std::string typeParams;
    if (!n.typeParams.empty()) {
        typeParams = "[";
        for (size_t i = 0; i < n.typeParams.size(); i++) {
            if (i > 0) typeParams += ", ";
            typeParams += n.typeParams[i];
        }
        typeParams += "]";
    }
    print("UnionDecl: " + n.name + typeParams);
    indent++; for (auto& [name, type] : n.fields) print(name + ": " + type); indent--;
}

void ASTPrinter::visit(EnumDecl& n) {
    print("EnumDecl: " + n.name);
    indent++;
    for (auto& [name, val] : n.variants) print(name + (val.has_value() ? " = " + std::to_string(*val) : ""));
    indent--;
}

void ASTPrinter::visit(TypeAlias& n) { 
    std::string str = "TypeAlias: " + n.name;
    
    // Print type parameters (including value parameters for dependent types)
    if (!n.typeParams.empty()) {
        str += "[";
        for (size_t i = 0; i < n.typeParams.size(); i++) {
            if (i > 0) str += ", ";
            str += n.typeParams[i].name;
            if (n.typeParams[i].isValue) {
                str += ": " + n.typeParams[i].kind;
            }
        }
        str += "]";
    }
    
    str += " = " + n.targetType;
    if (n.constraint) {
        str += " where <constraint>";
    }
    print(str);
    if (n.constraint) {
        indent++;
        n.constraint->accept(*this);
        indent--;
    }
}

void ASTPrinter::visit(TraitDecl& n) {
    std::string typeParams;
    if (!n.typeParams.empty()) {
        typeParams = "[";
        for (size_t i = 0; i < n.typeParams.size(); i++) {
            if (i > 0) typeParams += ", ";
            typeParams += n.typeParams[i];
        }
        typeParams += "]";
    }
    print("TraitDecl: " + n.name + typeParams);
    indent++; for (auto& method : n.methods) method->accept(*this); indent--;
}

void ASTPrinter::visit(ImplBlock& n) {
    std::string desc = n.traitName.empty() ? n.typeName : n.traitName + " for " + n.typeName;
    print("ImplBlock: " + desc);
    indent++; for (auto& method : n.methods) method->accept(*this); indent--;
}

void ASTPrinter::visit(ConceptDecl& n) {
    std::string typeParams;
    if (!n.typeParams.empty()) {
        typeParams = "[";
        for (size_t i = 0; i < n.typeParams.size(); i++) {
            if (i > 0) typeParams += ", ";
            typeParams += n.typeParams[i];
        }
        typeParams += "]";
    }
    std::string superConcepts;
    if (!n.superConcepts.empty()) {
        superConcepts = " : ";
        for (size_t i = 0; i < n.superConcepts.size(); i++) {
            if (i > 0) superConcepts += " + ";
            superConcepts += n.superConcepts[i];
        }
    }
    print("ConceptDecl: " + n.name + typeParams + superConcepts);
    indent++;
    for (const auto& req : n.requirements) {
        std::string reqStr = (req.isStatic ? "static " : "") + std::string("fn ") + req.name + "(";
        for (size_t i = 0; i < req.params.size(); i++) {
            if (i > 0) reqStr += ", ";
            reqStr += req.params[i].first + ": " + req.params[i].second;
        }
        reqStr += ") -> " + req.returnType;
        print(reqStr);
    }
    indent--;
}

void ASTPrinter::visit(UnsafeBlock& n) { print("UnsafeBlock"); indent++; n.body->accept(*this); indent--; }
void ASTPrinter::visit(ImportStmt& n) { print("ImportStmt: " + n.path + (n.alias.empty() ? "" : " as " + n.alias)); }

void ASTPrinter::visit(ExternDecl& n) {
    print("ExternDecl: " + n.abi + " " + n.library);
    indent++; for (auto& fn : n.functions) fn->accept(*this); indent--;
}

void ASTPrinter::visit(MacroDecl& n) { print("MacroDecl: " + n.name); }

void ASTPrinter::visit(SyntaxMacroDecl& n) {
    print("SyntaxMacroDecl: " + n.name);
    indent++; for (auto& decl : n.body) decl->accept(*this); indent--;
}

void ASTPrinter::visit(LayerDecl& n) {
    print("LayerDecl: " + n.name);
    indent++; for (auto& decl : n.declarations) decl->accept(*this); indent--;
}

void ASTPrinter::visit(UseStmt& n) { 
    std::string info = "UseStmt: " + n.layerName;
    if (!n.alias.empty()) info += " as " + n.alias;
    if (!n.importItems.empty()) {
        info += " {";
        for (size_t i = 0; i < n.importItems.size(); i++) {
            if (i > 0) info += ", ";
            info += n.importItems[i];
        }
        info += "}";
    }
    print(info);
}
void ASTPrinter::visit(ModuleDecl& n) { 
    print("ModuleDecl: " + n.name); 
    indent++; 
    for (auto& s : n.body) s->accept(*this); 
    indent--; 
}
void ASTPrinter::visit(DeleteStmt& n) { print("DeleteStmt"); indent++; n.expr->accept(*this); indent--; }
void ASTPrinter::visit(AsmStmt& n) { print("AsmStmt: " + n.code.substr(0, 50) + (n.code.size() > 50 ? "..." : "")); }
void ASTPrinter::visit(Program& n) { print("Program"); indent++; for (auto& s : n.statements) s->accept(*this); indent--; }

void printTokens(const std::vector<Token>& tokens) {
    std::cout << "=== Tokens ===\n";
    for (const auto& tok : tokens) std::cout << tok.toString() << "\n";
    std::cout << "\n";
}

void ASTPrinter::visit(AssignExpr& n) {
    print("AssignExpr: " + tokenTypeToString(n.op));
    indent++; n.target->accept(*this); n.value->accept(*this); indent--;
}

void ASTPrinter::visit(PropagateExpr& n) {
    print("PropagateExpr (?)");
    indent++; n.operand->accept(*this); indent--;
}

void ASTPrinter::visit(ChanSendExpr& n) {
    print("ChanSendExpr (<-)");
    indent++; 
    print("Channel:"); indent++; n.channel->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(ChanRecvExpr& n) {
    print("ChanRecvExpr (<-)");
    indent++; n.channel->accept(*this); indent--;
}

void ASTPrinter::visit(MakeChanExpr& n) {
    std::string info = "MakeChanExpr: chan[" + n.elementType;
    if (n.bufferSize > 0) info += ", " + std::to_string(n.bufferSize);
    info += "]";
    print(info);
}

void ASTPrinter::visit(MakeMutexExpr& n) {
    print("MakeMutexExpr: Mutex[" + n.elementType + "]");
}

void ASTPrinter::visit(MakeRWLockExpr& n) {
    print("MakeRWLockExpr: RWLock[" + n.elementType + "]");
}

void ASTPrinter::visit(MakeCondExpr& n) {
    (void)n;
    print("MakeCondExpr: Cond");
}

void ASTPrinter::visit(MakeSemaphoreExpr& n) {
    print("MakeSemaphoreExpr: Semaphore(" + std::to_string(n.initialCount) + ", " + std::to_string(n.maxCount) + ")");
}

void ASTPrinter::visit(MutexLockExpr& n) {
    print("MutexLockExpr");
    indent++; n.mutex->accept(*this); indent--;
}

void ASTPrinter::visit(MutexUnlockExpr& n) {
    print("MutexUnlockExpr");
    indent++; n.mutex->accept(*this); indent--;
}

void ASTPrinter::visit(RWLockReadExpr& n) {
    print("RWLockReadExpr");
    indent++; n.rwlock->accept(*this); indent--;
}

void ASTPrinter::visit(RWLockWriteExpr& n) {
    print("RWLockWriteExpr");
    indent++; n.rwlock->accept(*this); indent--;
}

void ASTPrinter::visit(RWLockUnlockExpr& n) {
    print("RWLockUnlockExpr");
    indent++; n.rwlock->accept(*this); indent--;
}

void ASTPrinter::visit(CondWaitExpr& n) {
    print("CondWaitExpr");
    indent++;
    print("Cond:"); indent++; n.cond->accept(*this); indent--;
    print("Mutex:"); indent++; n.mutex->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(CondSignalExpr& n) {
    print("CondSignalExpr");
    indent++; n.cond->accept(*this); indent--;
}

void ASTPrinter::visit(CondBroadcastExpr& n) {
    print("CondBroadcastExpr");
    indent++; n.cond->accept(*this); indent--;
}

void ASTPrinter::visit(SemAcquireExpr& n) {
    print("SemAcquireExpr");
    indent++; n.sem->accept(*this); indent--;
}

void ASTPrinter::visit(SemReleaseExpr& n) {
    print("SemReleaseExpr");
    indent++; n.sem->accept(*this); indent--;
}

void ASTPrinter::visit(SemTryAcquireExpr& n) {
    print("SemTryAcquireExpr");
    indent++; n.sem->accept(*this); indent--;
}

void ASTPrinter::visit(MakeAtomicExpr& n) {
    print("MakeAtomicExpr: Atomic[" + n.elementType + "]");
    if (n.initialValue) {
        indent++; 
        print("InitialValue:"); indent++; n.initialValue->accept(*this); indent--;
        indent--;
    }
}

void ASTPrinter::visit(AtomicLoadExpr& n) {
    print("AtomicLoadExpr");
    indent++; n.atomic->accept(*this); indent--;
}

void ASTPrinter::visit(AtomicStoreExpr& n) {
    print("AtomicStoreExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(AtomicSwapExpr& n) {
    print("AtomicSwapExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(AtomicCasExpr& n) {
    print("AtomicCasExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Expected:"); indent++; n.expected->accept(*this); indent--;
    print("Desired:"); indent++; n.desired->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(AtomicAddExpr& n) {
    print("AtomicAddExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(AtomicSubExpr& n) {
    print("AtomicSubExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(AtomicAndExpr& n) {
    print("AtomicAndExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(AtomicOrExpr& n) {
    print("AtomicOrExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(AtomicXorExpr& n) {
    print("AtomicXorExpr");
    indent++;
    print("Atomic:"); indent++; n.atomic->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

// Smart Pointer expressions
void ASTPrinter::visit(MakeBoxExpr& n) {
    print("MakeBoxExpr: Box[" + n.elementType + "]");
    indent++; n.value->accept(*this); indent--;
}

void ASTPrinter::visit(MakeRcExpr& n) {
    print("MakeRcExpr: Rc[" + n.elementType + "]");
    indent++; n.value->accept(*this); indent--;
}

void ASTPrinter::visit(MakeArcExpr& n) {
    print("MakeArcExpr: Arc[" + n.elementType + "]");
    indent++; n.value->accept(*this); indent--;
}

void ASTPrinter::visit(MakeWeakExpr& n) {
    print("MakeWeakExpr");
    indent++; n.source->accept(*this); indent--;
}

void ASTPrinter::visit(MakeCellExpr& n) {
    print("MakeCellExpr: Cell[" + n.elementType + "]");
    indent++; n.value->accept(*this); indent--;
}

void ASTPrinter::visit(MakeRefCellExpr& n) {
    print("MakeRefCellExpr: RefCell[" + n.elementType + "]");
    indent++; n.value->accept(*this); indent--;
}

void ASTPrinter::visit(LockStmt& n) {
    print("LockStmt");
    indent++;
    print("Mutex:"); indent++; n.mutex->accept(*this); indent--;
    print("Body:"); indent++; n.body->accept(*this); indent--;
    indent--;
}

// Advanced Concurrency - Future/Promise
void ASTPrinter::visit(MakeFutureExpr& n) {
    print("MakeFutureExpr: Future[" + n.elementType + "]");
}

void ASTPrinter::visit(FutureGetExpr& n) {
    print("FutureGetExpr");
    indent++; n.future->accept(*this); indent--;
}

void ASTPrinter::visit(FutureSetExpr& n) {
    print("FutureSetExpr");
    indent++;
    print("Future:"); indent++; n.future->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(FutureIsReadyExpr& n) {
    print("FutureIsReadyExpr");
    indent++; n.future->accept(*this); indent--;
}

// Advanced Concurrency - Thread Pool
void ASTPrinter::visit(MakeThreadPoolExpr& n) {
    print("MakeThreadPoolExpr");
    if (n.numWorkers) {
        indent++;
        print("Workers:"); indent++; n.numWorkers->accept(*this); indent--;
        indent--;
    }
}

void ASTPrinter::visit(ThreadPoolSubmitExpr& n) {
    print("ThreadPoolSubmitExpr");
    indent++;
    print("Pool:"); indent++; n.pool->accept(*this); indent--;
    print("Task:"); indent++; n.task->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(ThreadPoolShutdownExpr& n) {
    print("ThreadPoolShutdownExpr");
    indent++; n.pool->accept(*this); indent--;
}

// Advanced Concurrency - Select
void ASTPrinter::visit(SelectExpr& n) {
    print("SelectExpr");
    indent++;
    for (size_t i = 0; i < n.cases.size(); i++) {
        auto& c = n.cases[i];
        print("Case " + std::to_string(i) + " (" + (c.isSend ? "send" : "recv") + "):");
        indent++;
        print("Channel:"); indent++; c.channel->accept(*this); indent--;
        if (c.value) {
            print("Value:"); indent++; c.value->accept(*this); indent--;
        }
        if (c.body) {
            print("Body:"); indent++; c.body->accept(*this); indent--;
        }
        indent--;
    }
    if (n.defaultCase) {
        print("Default:"); indent++; n.defaultCase->accept(*this); indent--;
    }
    indent--;
}

// Advanced Concurrency - Timeout
void ASTPrinter::visit(TimeoutExpr& n) {
    print("TimeoutExpr");
    indent++;
    print("Operation:"); indent++; n.operation->accept(*this); indent--;
    print("Timeout:"); indent++; n.timeoutMs->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(ChanRecvTimeoutExpr& n) {
    print("ChanRecvTimeoutExpr");
    indent++;
    print("Channel:"); indent++; n.channel->accept(*this); indent--;
    print("Timeout:"); indent++; n.timeoutMs->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(ChanSendTimeoutExpr& n) {
    print("ChanSendTimeoutExpr");
    indent++;
    print("Channel:"); indent++; n.channel->accept(*this); indent--;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    print("Timeout:"); indent++; n.timeoutMs->accept(*this); indent--;
    indent--;
}

// Advanced Concurrency - Cancellation
void ASTPrinter::visit(MakeCancelTokenExpr& n) {
    (void)n;
    print("MakeCancelTokenExpr");
}

void ASTPrinter::visit(CancelExpr& n) {
    print("CancelExpr");
    indent++; n.token->accept(*this); indent--;
}

void ASTPrinter::visit(IsCancelledExpr& n) {
    print("IsCancelledExpr");
    indent++; n.token->accept(*this); indent--;
}

// Async Runtime - Event Loop and Task Management
void ASTPrinter::visit(AsyncRuntimeInitExpr& n) {
    print("AsyncRuntimeInitExpr");
    if (n.numWorkers) { indent++; n.numWorkers->accept(*this); indent--; }
}

void ASTPrinter::visit(AsyncRuntimeRunExpr& n) {
    (void)n;
    print("AsyncRuntimeRunExpr");
}

void ASTPrinter::visit(AsyncRuntimeShutdownExpr& n) {
    (void)n;
    print("AsyncRuntimeShutdownExpr");
}

void ASTPrinter::visit(AsyncSpawnExpr& n) {
    print("AsyncSpawnExpr");
    indent++; n.task->accept(*this); indent--;
}

void ASTPrinter::visit(AsyncSleepExpr& n) {
    print("AsyncSleepExpr");
    indent++; n.durationMs->accept(*this); indent--;
}

void ASTPrinter::visit(AsyncYieldExpr& n) {
    (void)n;
    print("AsyncYieldExpr");
}

// Syntax Redesign - New Expression Visitors
void ASTPrinter::visit(PlaceholderExpr& n) {
    (void)n;
    print("PlaceholderExpr: _");
}

void ASTPrinter::visit(InclusiveRangeExpr& n) {
    print("InclusiveRangeExpr (..=)");
    indent++;
    print("Start:"); indent++; n.start->accept(*this); indent--;
    print("End:"); indent++; n.end->accept(*this); indent--;
    if (n.step) {
        print("Step:"); indent++; n.step->accept(*this); indent--;
    }
    indent--;
}

void ASTPrinter::visit(SafeNavExpr& n) {
    print("SafeNavExpr: ?." + n.member);
    indent++; n.object->accept(*this); indent--;
}

void ASTPrinter::visit(TypeCheckExpr& n) {
    print("TypeCheckExpr: is " + n.typeName);
    indent++; n.value->accept(*this); indent--;
}

// Syntax Redesign - New Statement Visitors
void ASTPrinter::visit(LoopStmt& n) {
    std::string info = "LoopStmt";
    if (!n.label.empty()) info += " '" + n.label;
    print(info);
    indent++; n.body->accept(*this); indent--;
}

void ASTPrinter::visit(WithStmt& n) {
    std::string info = "WithStmt";
    if (!n.alias.empty()) info += " as " + n.alias;
    print(info);
    indent++;
    print("Resource:"); indent++; n.resource->accept(*this); indent--;
    print("Body:"); indent++; n.body->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(ScopeStmt& n) {
    std::string info = "ScopeStmt";
    if (!n.label.empty()) info += " '" + n.label;
    print(info);
    indent++;
    if (n.timeout) {
        print("Timeout:"); indent++; n.timeout->accept(*this); indent--;
    }
    print("Body:"); indent++; n.body->accept(*this); indent--;
    indent--;
}

void ASTPrinter::visit(RequireStmt& n) {
    print("RequireStmt" + (n.message.empty() ? "" : ": \"" + n.message + "\""));
    indent++; n.condition->accept(*this); indent--;
}

void ASTPrinter::visit(EnsureStmt& n) {
    print("EnsureStmt" + (n.message.empty() ? "" : ": \"" + n.message + "\""));
    indent++; n.condition->accept(*this); indent--;
}

void ASTPrinter::visit(InvariantStmt& n) {
    print("InvariantStmt" + (n.message.empty() ? "" : ": \"" + n.message + "\""));
    indent++; n.condition->accept(*this); indent--;
}

void ASTPrinter::visit(ComptimeBlock& n) {
    print("ComptimeBlock");
    indent++; n.body->accept(*this); indent--;
}

void ASTPrinter::visit(ComptimeAssertStmt& n) {
    std::string msg = n.message.empty() ? "" : ", \"" + n.message + "\"";
    print("ComptimeAssertStmt" + msg);
    indent++; n.condition->accept(*this); indent--;
}

void ASTPrinter::visit(EffectDecl& n) {
    std::string params;
    for (size_t i = 0; i < n.typeParams.size(); i++) {
        if (i > 0) params += ", ";
        params += n.typeParams[i];
    }
    print("EffectDecl: " + n.name + (params.empty() ? "" : "[" + params + "]"));
    indent++;
    for (auto& op : n.operations) {
        std::string opStr = "fn " + op.name + "(";
        for (size_t i = 0; i < op.params.size(); i++) {
            if (i > 0) opStr += ", ";
            opStr += op.params[i].first + ": " + op.params[i].second;
        }
        opStr += ") -> " + op.returnType;
        print(opStr);
    }
    indent--;
}

void ASTPrinter::visit(PerformEffectExpr& n) {
    print("PerformEffect: " + n.effectName + "." + n.opName);
    indent++;
    for (auto& arg : n.args) {
        arg->accept(*this);
    }
    indent--;
}

void ASTPrinter::visit(HandleExpr& n) {
    print("HandleExpr");
    indent++;
    print("Expression:");
    indent++; n.expr->accept(*this); indent--;
    for (auto& handler : n.handlers) {
        std::string handlerStr = handler.effectName + "." + handler.opName + "(";
        for (size_t i = 0; i < handler.paramNames.size(); i++) {
            if (i > 0) handlerStr += ", ";
            handlerStr += handler.paramNames[i];
        }
        handlerStr += ")";
        if (!handler.resumeParam.empty()) {
            handlerStr += " |" + handler.resumeParam + "|";
        }
        print("Handler: " + handlerStr);
        if (handler.body) {
            indent++; handler.body->accept(*this); indent--;
        }
    }
    indent--;
}

void ASTPrinter::visit(ResumeExpr& n) {
    print("ResumeExpr");
    if (n.value) {
        indent++; n.value->accept(*this); indent--;
    }
}

// Compile-Time Reflection
void ASTPrinter::visit(TypeMetadataExpr& n) {
    print("TypeMetadataExpr: " + n.typeName + "." + n.metadataKind);
}

void ASTPrinter::visit(FieldsOfExpr& n) {
    print("FieldsOfExpr: " + n.typeName);
}

void ASTPrinter::visit(MethodsOfExpr& n) {
    print("MethodsOfExpr: " + n.typeName);
}

void ASTPrinter::visit(HasFieldExpr& n) {
    print("HasFieldExpr: " + n.typeName);
    if (n.fieldName) {
        indent++; n.fieldName->accept(*this); indent--;
    }
}

void ASTPrinter::visit(HasMethodExpr& n) {
    print("HasMethodExpr: " + n.typeName);
    if (n.methodName) {
        indent++; n.methodName->accept(*this); indent--;
    }
}

void ASTPrinter::visit(FieldTypeExpr& n) {
    print("FieldTypeExpr: " + n.typeName);
    if (n.fieldName) {
        indent++; n.fieldName->accept(*this); indent--;
    }
}

// New Syntax Enhancements
void ASTPrinter::visit(IfLetStmt& n) {
    print("IfLetStmt: " + n.varName);
    indent++;
    print("Value:"); indent++; n.value->accept(*this); indent--;
    if (n.guard) {
        print("Guard:"); indent++; n.guard->accept(*this); indent--;
    }
    print("Then:"); indent++; n.thenBranch->accept(*this); indent--;
    if (n.elseBranch) {
        print("Else:"); indent++; n.elseBranch->accept(*this); indent--;
    }
    indent--;
}

void ASTPrinter::visit(MultiVarDecl& n) {
    std::string mod = n.isConst ? "const " : (n.isMutable ? "var " : "let ");
    std::string names;
    for (size_t i = 0; i < n.names.size(); i++) {
        if (i > 0) names += " = ";
        names += n.names[i];
    }
    print("MultiVarDecl: " + mod + names);
    if (n.initializer) { indent++; n.initializer->accept(*this); indent--; }
}

void ASTPrinter::visit(WalrusExpr& n) {
    print("WalrusExpr: " + n.varName + " :=");
    indent++; n.value->accept(*this); indent--;
}

} // namespace tyl
