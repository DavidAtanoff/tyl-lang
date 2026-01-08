// Tyl Compiler - Parser Trait/Impl Declarations
// Handles: trait, impl declarations

#include "parser_base.h"
#include "common/errors.h"

namespace tyl {

StmtPtr Parser::traitDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected trait name").lexeme;
    
    auto trait = std::make_unique<TraitDecl>(name, loc);
    
    // Parse type parameters including HKT: [T, F[_], M[_, _]]
    if (match(TokenType::LBRACKET)) {
        do {
            auto paramName = consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme;
            
            // Check for HKT syntax: F[_] or F[_, _]
            if (check(TokenType::LBRACKET)) {
                advance();  // consume the [
                size_t arity = 0;
                do {
                    if (check(TokenType::UNDERSCORE) || (check(TokenType::IDENTIFIER) && peek().lexeme == "_")) {
                        advance();  // consume _
                        arity++;
                    } else {
                        auto diag = errors::expectedToken("Expected '_' in type constructor parameter", 
                                                          tokenTypeToString(peek().type), peek().location);
                        throw TylDiagnosticError(diag);
                    }
                } while (match(TokenType::COMMA));
                consume(TokenType::RBRACKET, "Expected ']' after type constructor arity");
                
                // Create HKT type parameter
                HKTTypeParam hktParam(paramName, arity);
                
                // Check for trait bounds: F[_]: Functor
                if (match(TokenType::COLON)) {
                    do {
                        hktParam.bounds.push_back(consume(TokenType::IDENTIFIER, "Expected trait bound").lexeme);
                    } while (match(TokenType::PLUS));
                }
                
                trait->hktTypeParams.push_back(std::move(hktParam));
            } else {
                // Regular type parameter
                trait->typeParams.push_back(paramName);
            }
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
            auto fn = fnDeclaration(false);  // Trait methods don't require a body
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

StmtPtr Parser::conceptDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected concept name").lexeme;
    
    auto concept = std::make_unique<ConceptDecl>(name, loc);
    
    // Parse type parameters: concept Numeric[T]:
    if (match(TokenType::LBRACKET)) {
        do {
            concept->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    // Parse super concepts: concept Orderable[T]: Eq
    if (check(TokenType::COLON)) {
        size_t savedPos = current;
        advance();  // consume COLON
        
        if (check(TokenType::IDENTIFIER)) {
            // Check if this is super concepts or the body start
            // Super concepts are followed by another COLON for the body
            std::vector<std::string> potentialSupers;
            do {
                potentialSupers.push_back(consume(TokenType::IDENTIFIER, "Expected super concept name").lexeme);
            } while (match(TokenType::COMMA));
            
            if (check(TokenType::COLON)) {
                // These were super concepts
                concept->superConcepts = std::move(potentialSupers);
                consume(TokenType::COLON, "Expected ':' after super concepts");
            } else {
                // This was the body colon, restore and re-parse
                current = savedPos;
                consume(TokenType::COLON, "Expected ':' after concept name");
            }
        } else {
            // This was the body colon, already consumed
        }
    } else {
        consume(TokenType::COLON, "Expected ':' after concept name");
    }
    
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented concept body");
    skipNewlines();
    
    // Parse concept requirements (function signatures)
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        if (match(TokenType::FN)) {
            auto fnName = consume(TokenType::IDENTIFIER, "Expected function name").lexeme;
            ConceptRequirement req(fnName);
            
            // Parse parameters
            // Two styles supported:
            // 1. fn add(T, T) -> T        (type-only params)
            // 2. fn add a: T, b: T -> T   (named params)
            if (match(TokenType::LPAREN)) {
                // Parenthesized style: fn add(T, T) -> T
                if (!check(TokenType::RPAREN)) {
                    do {
                        std::string paramName;
                        std::string paramType;
                        
                        if (check(TokenType::IDENTIFIER)) {
                            auto first = advance().lexeme;
                            if (match(TokenType::COLON)) {
                                // Named parameter: name: Type
                                paramName = first;
                                paramType = parseType();
                            } else {
                                // Type-only parameter
                                paramName = "_p" + std::to_string(req.params.size());
                                paramType = first;
                            }
                        }
                        req.params.emplace_back(paramName, paramType);
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::RPAREN, "Expected ')' after parameters");
            } else {
                // Space-separated style: fn add a: T, b: T -> T
                while (!check(TokenType::ARROW) && !check(TokenType::NEWLINE) && !isAtEnd()) {
                    auto paramName = consume(TokenType::IDENTIFIER, "Expected parameter name").lexeme;
                    consume(TokenType::COLON, "Expected ':' after parameter name");
                    auto paramType = parseType();
                    req.params.emplace_back(paramName, paramType);
                    
                    if (!check(TokenType::ARROW) && !check(TokenType::NEWLINE)) {
                        match(TokenType::COMMA);
                    }
                }
            }
            
            // Parse return type
            if (match(TokenType::ARROW)) {
                req.returnType = parseType();
            }
            
            // Check if this is a static function (no self parameter)
            req.isStatic = true;
            for (const auto& param : req.params) {
                if (param.first == "self") {
                    req.isStatic = false;
                    break;
                }
            }
            
            concept->requirements.push_back(std::move(req));
        }
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of concept");
    return concept;
}

} // namespace tyl
