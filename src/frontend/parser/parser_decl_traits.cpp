// Tyl Compiler - Parser Trait/Impl Declarations
// Handles: trait, impl declarations

#include "parser_base.h"
#include "common/errors.h"

namespace tyl {

StmtPtr Parser::traitDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected trait name").lexeme;
    
    auto trait = std::make_unique<TraitDecl>(name, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            trait->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    // Parse super traits: trait Foo: Bar, Baz
    if (check(TokenType::COLON)) {
        size_t savedPos = current;
        advance();  // consume COLON
        
        if (check(TokenType::IDENTIFIER)) {
            // This is super traits
            do {
                trait->superTraits.push_back(consume(TokenType::IDENTIFIER, "Expected super trait name").lexeme);
            } while (match(TokenType::COMMA));
            
            // Now expect the block colon
            consume(TokenType::COLON, "Expected ':' after super traits");
        } else {
            // This was the block colon, restore position
            current = savedPos;
            consume(TokenType::COLON, "Expected ':' after trait name");
        }
    } else {
        consume(TokenType::COLON, "Expected ':' after trait name");
    }
    
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented trait body");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        if (match(TokenType::FN)) {
            auto fn = fnDeclaration();
            trait->methods.push_back(std::unique_ptr<FnDecl>(static_cast<FnDecl*>(fn.release())));
        }
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of trait");
    return trait;
}

StmtPtr Parser::implDeclaration() {
    auto loc = previous().location;
    
    auto firstIdent = consume(TokenType::IDENTIFIER, "Expected trait or type name").lexeme;
    
    std::string traitName;
    std::string typeName;
    
    if (check(TokenType::FOR)) {
        advance();
        traitName = firstIdent;
        typeName = consume(TokenType::IDENTIFIER, "Expected type name").lexeme;
    } else {
        typeName = firstIdent;
    }
    
    auto impl = std::make_unique<ImplBlock>(traitName, typeName, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            impl->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after impl declaration");
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented impl body");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        if (match(TokenType::FN)) {
            auto fn = fnDeclaration();
            impl->methods.push_back(std::unique_ptr<FnDecl>(static_cast<FnDecl*>(fn.release())));
        }
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of impl");
    return impl;
}

} // namespace tyl
