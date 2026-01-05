// Tyl Compiler - Error Codes and Common Diagnostics
#ifndef TYL_ERRORS_H
#define TYL_ERRORS_H

#include "diagnostics.h"
#include "common/common.h"

namespace tyl {

// Error code prefixes:
// E0xxx - Lexer errors
// E1xxx - Parser errors  
// E2xxx - Type errors
// E3xxx - Semantic errors
// E4xxx - Codegen errors
// E5xxx - Linker errors
// E6xxx - Runtime errors
// E9xxx - IO/System errors

namespace errors {

// Helper to create diagnostic from SourceLocation
inline SourceSpan toSpan(const SourceLocation& loc, int len = 1) {
    return SourceSpan::fromLocation(loc.filename, loc.line, loc.column, len);
}

// === Lexer Errors (E0xxx) ===

inline Diagnostic unexpectedChar(char c, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Lexer;
    d.code = "E0001";
    d.message = std::string("unexpected character '") + c + "'";
    d.span = toSpan(loc);
    
    if (c == '@') {
        d.suggestion = "did you mean to use a decorator? Flex doesn't support decorators yet";
    } else if (c == '$') {
        d.suggestion = "variable names don't need $ prefix in Flex";
    }
    return d;
}

inline Diagnostic unterminatedString(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Lexer;
    d.code = "E0002";
    d.message = "unterminated string literal";
    d.span = toSpan(loc);
    d.hint = "strings must be closed with a matching quote character";
    return d;
}

inline Diagnostic unterminatedInterpolation(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Lexer;
    d.code = "E0003";
    d.message = "unterminated string interpolation";
    d.span = toSpan(loc);
    d.hint = "interpolations must have matching braces: \"Hello {name}\"";
    return d;
}

inline Diagnostic inconsistentIndentation(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Lexer;
    d.code = "E0004";
    d.message = "inconsistent indentation";
    d.span = toSpan(loc);
    d.hint = "use consistent spaces or tabs for indentation throughout the file";
    return d;
}

// === Parser Errors (E1xxx) ===

inline Diagnostic expectedToken(const std::string& expected, const std::string& got, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Parser;
    d.code = "E1001";
    d.message = "expected " + expected + ", found " + got;
    d.span = toSpan(loc);
    return d;
}

inline Diagnostic expectedExpression(const std::string& got, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Parser;
    d.code = "E1002";
    d.message = "expected expression, found " + got;
    d.span = toSpan(loc);
    return d;
}

inline Diagnostic expectedFunctionBody(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Parser;
    d.code = "E1003";
    d.message = "expected ':', '=>', or '=' after function signature";
    d.span = toSpan(loc);
    d.hint = "use ':' for multi-line body, '=>' for single expression, or '=' for assignment";
    d.suggestion = "fn add a, b => a + b  // single expression\nfn add a, b:          // multi-line body";
    return d;
}

inline Diagnostic unexpectedToken(const std::string& token, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Parser;
    d.code = "E1004";
    d.message = "unexpected token '" + token + "'";
    d.span = toSpan(loc);
    return d;
}

inline Diagnostic invalidAssignmentTarget(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Parser;
    d.code = "E1005";
    d.message = "invalid assignment target";
    d.span = toSpan(loc);
    d.hint = "only variables and member accesses can be assigned to";
    return d;
}

// === Type Errors (E2xxx) ===

inline Diagnostic typeMismatch(const std::string& expected, const std::string& got, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Type;
    d.code = "E2001";
    d.message = "type mismatch: expected " + expected + ", found " + got;
    d.span = toSpan(loc);
    return d;
}

inline Diagnostic undefinedVariable(const std::string& name, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Type;
    d.code = "E2002";
    d.message = "undefined variable '" + name + "'";
    d.span = toSpan(loc, (int)name.size());
    return d;
}

inline Diagnostic undefinedFunction(const std::string& name, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Type;
    d.code = "E2003";
    d.message = "undefined function '" + name + "'";
    d.span = toSpan(loc, (int)name.size());
    return d;
}

inline Diagnostic cannotMutateImmutable(const std::string& name, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Type;
    d.code = "E2004";
    d.message = "cannot mutate immutable variable '" + name + "'";
    d.span = toSpan(loc, (int)name.size());
    d.hint = "declare with 'mut' to make it mutable: mut " + name + " = ...";
    return d;
}

inline Diagnostic wrongArgumentCount(const std::string& name, int expected, int got, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Type;
    d.code = "E2005";
    d.message = "function '" + name + "' expects " + std::to_string(expected) + 
                " argument(s), but " + std::to_string(got) + " were provided";
    d.span = toSpan(loc);
    return d;
}

// === Semantic Errors (E3xxx) ===

inline Diagnostic duplicateDefinition(const std::string& name, const SourceLocation& loc, const SourceLocation& prevLoc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Semantic;
    d.code = "E3001";
    d.message = "duplicate definition of '" + name + "'";
    d.span = toSpan(loc, (int)name.size());
    
    Diagnostic note;
    note.level = DiagnosticLevel::Note;
    note.message = "previous definition here";
    note.span = toSpan(prevLoc, (int)name.size());
    d.notes.push_back(note);
    
    return d;
}

inline Diagnostic breakOutsideLoop(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Semantic;
    d.code = "E3002";
    d.message = "'break' outside of loop";
    d.span = toSpan(loc, 5);
    d.hint = "'break' can only be used inside 'while' or 'for' loops";
    return d;
}

inline Diagnostic continueOutsideLoop(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Semantic;
    d.code = "E3003";
    d.message = "'continue' outside of loop";
    d.span = toSpan(loc, 8);
    d.hint = "'continue' can only be used inside 'while' or 'for' loops";
    return d;
}

inline Diagnostic returnOutsideFunction(const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Semantic;
    d.code = "E3004";
    d.message = "'return' outside of function";
    d.span = toSpan(loc, 6);
    return d;
}

// === Codegen Errors (E4xxx) ===

inline Diagnostic codegenFailed(const std::string& reason, const SourceLocation& loc) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Codegen;
    d.code = "E4001";
    d.message = "code generation failed: " + reason;
    d.span = toSpan(loc);
    return d;
}

// === Linker Errors (E5xxx) ===

inline Diagnostic undefinedSymbol(const std::string& name) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Linker;
    d.code = "E5001";
    d.message = "undefined symbol '" + name + "'";
    d.span = {};
    return d;
}

inline Diagnostic duplicateSymbol(const std::string& name) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Linker;
    d.code = "E5002";
    d.message = "duplicate symbol '" + name + "'";
    d.span = {};
    return d;
}

// === Runtime Errors (E6xxx) ===

inline Diagnostic divisionByZero(const SourceLocation& loc = {}) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Runtime;
    d.code = "E6001";
    d.message = "division by zero";
    d.span = toSpan(loc);
    return d;
}

inline Diagnostic indexOutOfBounds(int index, int size, const SourceLocation& loc = {}) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Runtime;
    d.code = "E6002";
    d.message = "index " + std::to_string(index) + " out of bounds for list of size " + std::to_string(size);
    d.span = toSpan(loc);
    return d;
}

inline Diagnostic nullPointer(const SourceLocation& loc = {}) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::Runtime;
    d.code = "E6003";
    d.message = "null pointer dereference";
    d.span = toSpan(loc);
    return d;
}

inline Diagnostic stackOverflow() {
    Diagnostic d;
    d.level = DiagnosticLevel::Fatal;
    d.category = DiagnosticCategory::Runtime;
    d.code = "E6004";
    d.message = "stack overflow";
    d.span = {};
    d.hint = "this usually indicates infinite recursion";
    return d;
}

// === IO Errors (E9xxx) ===

inline Diagnostic cannotOpenFile(const std::string& path) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::IO;
    d.code = "E9001";
    d.message = "cannot open file '" + path + "'";
    d.span = {};
    return d;
}

inline Diagnostic cannotWriteFile(const std::string& path) {
    Diagnostic d;
    d.level = DiagnosticLevel::Error;
    d.category = DiagnosticCategory::IO;
    d.code = "E9002";
    d.message = "cannot write to file '" + path + "'";
    d.span = {};
    return d;
}

} // namespace errors

// Enhanced TylError that can carry a Diagnostic
class TylDiagnosticError : public std::runtime_error {
public:
    Diagnostic diagnostic;
    
    TylDiagnosticError(Diagnostic d)
        : std::runtime_error(d.message), diagnostic(std::move(d)) {}
    
    void render(std::ostream& out = std::cerr) const {
        DiagnosticRenderer renderer;
        renderer.render(diagnostic, out);
    }
};

} // namespace tyl

#endif // TYL_ERRORS_H
