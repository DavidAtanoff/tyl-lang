// Tyl Compiler - Parser Module/Import Declarations
// Handles: use, import, module, extern declarations

#include "parser_base.h"
#include "common/errors.h"

namespace tyl {

StmtPtr Parser::useStatement() {
    auto loc = previous().location;
    
    // use layer "name"
    if (match(TokenType::LAYER)) {
        auto name = consume(TokenType::STRING, "Expected layer name string").lexeme;
        if (auto* str = std::get_if<std::string>(&previous().literal)) {
            name = *str;
        }
        match(TokenType::NEWLINE);
        auto stmt = std::make_unique<UseStmt>(name, loc);
        stmt->isLayer = true;
        return stmt;
    }
    
    // use "file.tyl" - file import
    if (check(TokenType::STRING)) {
        auto path = std::get<std::string>(advance().literal);
        
        // Check for alias: use "file.tyl" as name
        std::string alias;
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "as") {
            advance();  // consume 'as'
            alias = consume(TokenType::IDENTIFIER, "Expected alias name").lexeme;
        }
        
        match(TokenType::NEWLINE);
        auto stmt = std::make_unique<UseStmt>(path, loc);
        stmt->isFileImport = true;
        stmt->alias = alias;
        return stmt;
    }
    
    // use module::submodule or use module::{item1, item2}
    std::string path;
    path = consume(TokenType::IDENTIFIER, "Expected module name or string path").lexeme;
    
    while (match(TokenType::DOUBLE_COLON)) {
        // Check for selective import: use math::{sin, cos}
        if (match(TokenType::LBRACE)) {
            auto stmt = std::make_unique<UseStmt>(path, loc);
            
            // Parse import items
            if (!check(TokenType::RBRACE)) {
                do {
                    skipNewlines();
                    stmt->importItems.push_back(
                        consume(TokenType::IDENTIFIER, "Expected import item").lexeme
                    );
                } while (match(TokenType::COMMA));
            }
            skipNewlines();
            consume(TokenType::RBRACE, "Expected '}' after import items");
            match(TokenType::NEWLINE);
            return stmt;
        }
        
        // Check for wildcard: use math::*
        if (match(TokenType::STAR)) {
            auto stmt = std::make_unique<UseStmt>(path, loc);
            stmt->importItems.push_back("*");  // Wildcard marker
            match(TokenType::NEWLINE);
            return stmt;
        }
        
        path += "::" + consume(TokenType::IDENTIFIER, "Expected identifier").lexeme;
    }
    
    // Check for alias: use math as m
    std::string alias;
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "as") {
        advance();  // consume 'as'
        alias = consume(TokenType::IDENTIFIER, "Expected alias name").lexeme;
    }
    
    match(TokenType::NEWLINE);
    auto stmt = std::make_unique<UseStmt>(path, loc);
    stmt->alias = alias;
    return stmt;
}

StmtPtr Parser::moduleDeclaration() {
    auto loc = previous().location;
    
    // module name:
    std::string name = consume(TokenType::IDENTIFIER, "Expected module name").lexeme;
    
    // Allow nested module names: module math::calculus:
    while (match(TokenType::DOUBLE_COLON)) {
        name += "::" + consume(TokenType::IDENTIFIER, "Expected module name").lexeme;
    }
    
    consume(TokenType::COLON, "Expected ':' after module name");
    match(TokenType::NEWLINE);
    
    auto mod = std::make_unique<ModuleDecl>(name, loc);
    
    // Parse module body (indented block)
    if (match(TokenType::INDENT)) {
        while (!check(TokenType::DEDENT) && !isAtEnd()) {
            mod->body.push_back(declaration());
            skipNewlines();
        }
        match(TokenType::DEDENT);
    }
    
    return mod;
}

StmtPtr Parser::importStatement() {
    auto loc = previous().location;
    auto path = consume(TokenType::STRING, "Expected import path").lexeme;
    if (auto* str = std::get_if<std::string>(&previous().literal)) {
        path = *str;
    }
    
    auto imp = std::make_unique<ImportStmt>(path, loc);
    
    if (match(TokenType::IDENTIFIER) && previous().lexeme == "as") {
        imp->alias = consume(TokenType::IDENTIFIER, "Expected alias name").lexeme;
    }
    
    match(TokenType::NEWLINE);
    return imp;
}

StmtPtr Parser::externDeclaration() {
    auto loc = previous().location;
    
    std::string abi = "C";
    std::string library;
    
    // Parse ABI and/or library specification
    if (check(TokenType::STRING)) {
        std::string first = std::get<std::string>(advance().literal);
        
        // Check if this is an ABI specifier or a library
        if (first == "C" || first == "cdecl" || first == "stdcall" || first == "fastcall" || first == "win64") {
            abi = first;
            // Check for optional library
            if (check(TokenType::STRING)) {
                library = std::get<std::string>(advance().literal);
            }
        } else {
            // It's a library name, ABI defaults to "C"
            library = first;
        }
    }
    
    consume(TokenType::COLON, "Expected ':' after extern");
    match(TokenType::NEWLINE);
    
    auto ext = std::make_unique<ExternDecl>(abi, library, loc);
    consume(TokenType::INDENT, "Expected indented extern block");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        if (match(TokenType::FN)) {
            auto fn = externFnDeclaration();
            ext->functions.push_back(std::unique_ptr<FnDecl>(static_cast<FnDecl*>(fn.release())));
        }
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of extern block");
    return ext;
}

StmtPtr Parser::externFnDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected function name").lexeme;
    
    auto fn = std::make_unique<FnDecl>(name, loc);
    fn->isExtern = true;
    
    // Parse parameters
    if (match(TokenType::LPAREN)) {
        // New style with parentheses
        while (!check(TokenType::RPAREN) && !isAtEnd()) {
            // Check for variadic (...)
            if (match(TokenType::DOTDOT)) {
                if (match(TokenType::DOT)) {
                    fn->params.emplace_back("...", "...");
                }
                break;
            }
            
            std::string paramName;
            std::string paramType;
            
            if (check(TokenType::IDENTIFIER)) {
                paramName = advance().lexeme;
                if (match(TokenType::COLON)) {
                    paramType = parseType();
                } else {
                    // Just a type, no name
                    paramType = paramName;
                    paramName = "_p" + std::to_string(fn->params.size());
                }
            } else if (check(TokenType::STAR)) {
                // Pointer type without name
                paramType = parseType();
                paramName = "_p" + std::to_string(fn->params.size());
            }
            
            fn->params.emplace_back(paramName, paramType);
            
            if (!match(TokenType::COMMA)) break;
        }
        consume(TokenType::RPAREN, "Expected ')' after parameters");
    } else {
        // Old style: space-separated params without parens
        fn->params = parseParams();
    }
    
    if (match(TokenType::ARROW)) {
        fn->returnType = parseType();
    }
    
    match(TokenType::NEWLINE);
    return fn;
}

} // namespace tyl
