// Tyl Compiler - Parser Base Header
// This header contains the Parser class declaration and is included by all parser modules
#ifndef TYL_PARSER_BASE_H
#define TYL_PARSER_BASE_H

#include "frontend/ast/ast.h"
#include "frontend/token/token.h"

namespace tyl {

class Parser {
public:
    Parser(std::vector<Token> tokens);
    std::unique_ptr<Program> parse();
    
    // Pratt parser precedence levels (public for helper functions)
    enum class Precedence {
        NONE = 0,
        ASSIGNMENT,     // =
        TERNARY,        // if/else
        NULL_COALESCE,  // ??
        PIPE,           // |>
        OR,             // or ||
        AND,            // and &&
        BIT_OR,         // |
        BIT_XOR,        // ^
        BIT_AND,        // &
        EQUALITY,       // == !=
        COMPARISON,     // < > <= >=
        RANGE,          // ..
        TERM,           // + -
        FACTOR,         // * / %
        UNARY,          // - ! ~ & *
        POSTFIX,        // . [] () ! ?
        PRIMARY
    };
    
private:
    std::vector<Token> tokens;
    size_t current = 0;
    bool inConstraintContext_ = false;  // Don't transform placeholders in constraint expressions
    
    // Token navigation (parser_core.cpp)
    Token& peek();
    Token& previous();
    bool isAtEnd();
    Token& advance();
    bool check(TokenType type);
    bool match(TokenType type);
    bool match(std::initializer_list<TokenType> types);
    Token& consume(TokenType type, const std::string& message);
    void skipNewlines();
    void synchronize();
    bool isAtStatementBoundary();
    void preScanSyntaxDeclarations();
    
    // Declaration parsing (parser_declarations.cpp)
    StmtPtr declaration();
    StmtPtr fnDeclaration(bool requireBody = true);
    StmtPtr externFnDeclaration();
    StmtPtr recordDeclaration();
    StmtPtr unionDeclaration();
    StmtPtr enumDeclaration();
    StmtPtr typeAliasDeclaration();
    StmtPtr traitDeclaration();
    StmtPtr conceptDeclaration();
    StmtPtr implDeclaration();
    StmtPtr useStatement();
    StmtPtr importStatement();
    StmtPtr externDeclaration();
    StmtPtr macroDeclaration();
    StmtPtr syntaxMacroDeclaration();
    StmtPtr layerDeclaration();
    StmtPtr moduleDeclaration();
    StmtPtr unsafeBlock();
    StmtPtr asmStatement();
    StmtPtr varDeclaration();
    
    // Statement parsing (parser_statements.cpp)
    StmtPtr statement();
    StmtPtr expressionStatement();
    StmtPtr ifStatement();
    StmtPtr whileStatement(const std::string& label = "");
    StmtPtr forStatement(const std::string& label = "");
    StmtPtr matchStatement();
    StmtPtr returnStatement();
    StmtPtr breakStatement();
    StmtPtr continueStatement();
    StmtPtr deleteStatement();
    StmtPtr lockStatement();
    StmtPtr block();
    StmtPtr braceBlock();  // Brace-delimited block { ... }
    // New syntax redesign statements
    StmtPtr unlessStatement();
    StmtPtr loopStatement(const std::string& label = "");
    StmtPtr withStatement();
    StmtPtr scopeStatement();
    StmtPtr requireStatement();
    StmtPtr ensureStatement();
    StmtPtr comptimeBlock();
    StmtPtr effectDeclaration();
    
    // Pratt parser expression parsing (parser_expressions.cpp)
    ExprPtr expression();
    ExprPtr parsePrecedence(Precedence minPrec);
    ExprPtr parsePrefix();
    ExprPtr parseInfix(ExprPtr left, Precedence prec);
    ExprPtr parseTernary(ExprPtr thenExpr);
    ExprPtr parseCast(ExprPtr expr);
    ExprPtr parseMemberAccess(ExprPtr object, SourceLocation loc);
    ExprPtr parseIndexAccess(ExprPtr object, SourceLocation loc);
    ExprPtr parseCall(ExprPtr callee, SourceLocation loc);
    ExprPtr parsePipe(ExprPtr left, ExprPtr right, SourceLocation loc);
    ExprPtr parseNew(SourceLocation loc);
    ExprPtr primary();
    ExprPtr listLiteral();
    ExprPtr recordLiteral();
    ExprPtr lambda();
    ExprPtr parseArrowLambda(const std::string& firstParam, SourceLocation loc);
    
    // Legacy compatibility (redirect to Pratt parser)
    ExprPtr assignment();
    ExprPtr ternary();
    ExprPtr nullCoalesce();
    ExprPtr userInfixExpr();
    ExprPtr pipeExpr();
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr bitwiseOr();
    ExprPtr bitwiseXor();
    ExprPtr bitwiseAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr range();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr postfix();
    
    // Type and helper parsing (parser_types.cpp)
    std::string parseType();
    std::vector<std::pair<std::string, std::string>> parseParams();
    void parseCallArgs(CallExpr* call);
    ExprPtr parseDSLBlock(const std::string& dslName, SourceLocation loc);
    std::string captureRawBlock();
};

} // namespace tyl

#endif // TYL_PARSER_BASE_H
