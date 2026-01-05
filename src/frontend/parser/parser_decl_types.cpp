// Tyl Compiler - Parser Type Declarations
// Handles: record, union, enum, type alias declarations

#include "parser_base.h"
#include "common/errors.h"

namespace tyl {

StmtPtr Parser::recordDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected record name").lexeme;
    
    auto rec = std::make_unique<RecordDecl>(name, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            rec->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after record name");
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented record fields");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        auto fieldName = consume(TokenType::IDENTIFIER, "Expected field name").lexeme;
        std::string fieldType;
        BitfieldSpec bitfield;
        
        if (match(TokenType::COLON)) {
            fieldType = parseType();
            
            // Check for bitfield specification: field: int : 4
            if (match(TokenType::COLON)) {
                auto bitWidthTok = consume(TokenType::INTEGER, "Expected bit width for bitfield");
                bitfield.bitWidth = static_cast<int>(std::get<int64_t>(bitWidthTok.literal));
            }
        }
        rec->fields.emplace_back(fieldName, fieldType);
        rec->bitfields.push_back(bitfield);
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of record");
    return rec;
}

StmtPtr Parser::unionDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected union name").lexeme;
    
    auto un = std::make_unique<UnionDecl>(name, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            un->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after union name");
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented union fields");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        auto fieldName = consume(TokenType::IDENTIFIER, "Expected field name").lexeme;
        std::string fieldType;
        if (match(TokenType::COLON)) {
            fieldType = parseType();
        }
        un->fields.emplace_back(fieldName, fieldType);
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of union");
    return un;
}

StmtPtr Parser::enumDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected enum name").lexeme;
    
    auto en = std::make_unique<EnumDecl>(name, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            en->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after enum name");
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented enum variants");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        auto variantName = consume(TokenType::IDENTIFIER, "Expected variant name").lexeme;
        std::optional<int64_t> value;
        if (match(TokenType::ASSIGN)) {
            // Handle negative values: -1, -2, etc.
            bool isNegative = match(TokenType::MINUS);
            auto valTok = consume(TokenType::INTEGER, "Expected integer value");
            int64_t intValue = std::get<int64_t>(valTok.literal);
            value = isNegative ? -intValue : intValue;
        }
        en->variants.emplace_back(variantName, value);
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of enum");
    return en;
}

StmtPtr Parser::typeAliasDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected type name").lexeme;
    
    auto alias = std::make_unique<TypeAlias>(name, "", loc);
    
    // Parse type parameters: type Vector[T, N: int] = ...
    if (match(TokenType::LBRACKET)) {
        do {
            auto paramName = consume(TokenType::IDENTIFIER, "Expected type parameter name").lexeme;
            
            // Check if this is a value parameter: N: int
            if (match(TokenType::COLON)) {
                auto paramType = parseType();
                alias->typeParams.emplace_back(paramName, paramType, true);  // isValue = true
            } else {
                alias->typeParams.emplace_back(paramName, "type", false);  // Regular type param
            }
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::ASSIGN, "Expected '=' after type name");
    
    // Check for opaque type: type Handle = opaque
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "opaque") {
        advance();  // consume 'opaque'
        match(TokenType::NEWLINE);
        alias->targetType = "opaque";
        return alias;
    }
    
    alias->targetType = parseType();
    
    // Parse where clause: type NonEmpty[T] = [T] where len(_) > 0
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "where") {
        advance();  // consume 'where'
        inConstraintContext_ = true;  // Don't transform placeholders in constraints
        alias->constraint = expression();
        inConstraintContext_ = false;
    }
    
    match(TokenType::NEWLINE);
    return alias;
}

} // namespace tyl
