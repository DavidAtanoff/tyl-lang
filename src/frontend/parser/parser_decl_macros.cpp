// Tyl Compiler - Parser Macro Declarations
// Handles: macro, syntax macro, layer, unsafe, asm declarations

#include "parser_base.h"
#include "common/errors.h"

namespace tyl {

StmtPtr Parser::macroDeclaration() {
    auto loc = previous().location;
    
    // Infix operator macro: macro infix "<=>" 50 left right: ...
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "infix") {
        advance();
        
        auto opSymbol = consume(TokenType::STRING, "Expected operator symbol string").lexeme;
        if (auto* str = std::get_if<std::string>(&previous().literal)) {
            opSymbol = *str;
        }
        
        int precedence = 50;
        if (check(TokenType::INTEGER)) {
            precedence = static_cast<int>(std::get<int64_t>(advance().literal));
        }
        
        auto mac = std::make_unique<MacroDecl>("infix_" + opSymbol, loc);
        mac->isOperator = true;
        mac->isInfix = true;
        mac->operatorSymbol = opSymbol;
        mac->precedence = precedence;
        
        if (check(TokenType::IDENTIFIER)) {
            mac->params.push_back(advance().lexeme);
        } else {
            mac->params.push_back("left");
        }
        if (check(TokenType::IDENTIFIER)) {
            mac->params.push_back(advance().lexeme);
        } else {
            mac->params.push_back("right");
        }
        
        consume(TokenType::COLON, "Expected ':' after infix macro signature");
        match(TokenType::NEWLINE);
        
        if (check(TokenType::INDENT)) {
            consume(TokenType::INDENT, "Expected indented macro body");
            skipNewlines();
            while (!check(TokenType::DEDENT) && !isAtEnd()) {
                mac->body.push_back(declaration());
                skipNewlines();
            }
            consume(TokenType::DEDENT, "Expected end of macro");
        } else {
            auto expr = expression();
            mac->body.push_back(std::make_unique<ExprStmt>(std::move(expr), loc));
            match(TokenType::NEWLINE);
        }
        
        return mac;
    }
    
    // Regular macro
    auto name = consume(TokenType::IDENTIFIER, "Expected macro name").lexeme;
    
    auto mac = std::make_unique<MacroDecl>(name, loc);
    
    while (check(TokenType::IDENTIFIER)) {
        mac->params.push_back(advance().lexeme);
    }
    
    consume(TokenType::COLON, "Expected ':' after macro signature");
    match(TokenType::NEWLINE);
    
    if (check(TokenType::INDENT)) {
        consume(TokenType::INDENT, "Expected indented macro body");
        skipNewlines();
        while (!check(TokenType::DEDENT) && !isAtEnd()) {
            mac->body.push_back(declaration());
            skipNewlines();
        }
        consume(TokenType::DEDENT, "Expected end of macro");
    }
    
    return mac;
}

StmtPtr Parser::syntaxMacroDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected syntax macro name").lexeme;
    
    auto syntaxMac = std::make_unique<SyntaxMacroDecl>(name, loc);
    
    if (match(TokenType::DOUBLE_ARROW)) {
        std::string transformExpr;
        while (!check(TokenType::NEWLINE) && !isAtEnd()) {
            transformExpr += peek().lexeme;
            advance();
        }
        syntaxMac->transformExpr = transformExpr;
        match(TokenType::NEWLINE);
        return syntaxMac;
    }
    
    if (match(TokenType::COLON)) {
        match(TokenType::NEWLINE);
        
        if (check(TokenType::INDENT)) {
            consume(TokenType::INDENT, "Expected indented syntax macro body");
            skipNewlines();
            
            if (check(TokenType::IDENTIFIER) && peek().lexeme == "transform") {
                advance();
                if (match(TokenType::DOUBLE_ARROW)) {
                    std::string transformExpr;
                    while (!check(TokenType::NEWLINE) && !isAtEnd()) {
                        if (!transformExpr.empty()) transformExpr += " ";
                        transformExpr += peek().lexeme;
                        advance();
                    }
                    syntaxMac->transformExpr = transformExpr;
                    match(TokenType::NEWLINE);
                    skipNewlines();
                }
            }
            
            while (!check(TokenType::DEDENT) && !isAtEnd()) {
                syntaxMac->body.push_back(declaration());
                skipNewlines();
            }
            consume(TokenType::DEDENT, "Expected end of syntax macro");
        }
    } else {
        match(TokenType::NEWLINE);
    }
    
    return syntaxMac;
}

StmtPtr Parser::layerDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected layer name").lexeme;
    consume(TokenType::COLON, "Expected ':' after layer name");
    match(TokenType::NEWLINE);
    
    auto layer = std::make_unique<LayerDecl>(name, loc);
    consume(TokenType::INDENT, "Expected indented layer body");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        layer->declarations.push_back(declaration());
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of layer");
    return layer;
}

StmtPtr Parser::unsafeBlock() {
    auto loc = previous().location;
    consume(TokenType::COLON, "Expected ':' after unsafe");
    return block();
}

StmtPtr Parser::asmStatement() {
    auto loc = previous().location;
    
    // asm: or asm "instruction"
    if (match(TokenType::STRING)) {
        auto code = std::get<std::string>(previous().literal);
        match(TokenType::NEWLINE);
        return std::make_unique<AsmStmt>(code, loc);
    }
    
    consume(TokenType::COLON, "Expected ':' after asm");
    match(TokenType::NEWLINE);
    
    std::string asmCode;
    
    if (match(TokenType::INDENT)) {
        while (!check(TokenType::DEDENT) && !isAtEnd()) {
            if (check(TokenType::STRING)) {
                if (!asmCode.empty()) asmCode += "\n";
                asmCode += std::get<std::string>(advance().literal);
            }
            match(TokenType::NEWLINE);
            skipNewlines();
        }
        consume(TokenType::DEDENT, "Expected end of asm block");
    }
    
    return std::make_unique<AsmStmt>(asmCode, loc);
}

} // namespace tyl
