// Tyl Compiler - Lexer Scanning
// Token scanning methods

#include "lexer_base.h"
#include "common/errors.h"
#include <set>

namespace tyl {

void Lexer::handleIndentation() {
    int indent = 0;
    while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
        if (peek() == ' ') indent++;
        else indent += 4; // Tab = 4 spaces
        advance();
    }
    
    if (peek() == '\n' || (peek() == '/' && peekNext() == '/')) return;
    
    int currentIndent = indentStack.top();
    
    if (indent > currentIndent) {
        indentStack.push(indent);
        tokens.emplace_back(TokenType::INDENT, "", SourceLocation{filename, line, 1});
    } else {
        while (indent < indentStack.top()) {
            indentStack.pop();
            tokens.emplace_back(TokenType::DEDENT, "", SourceLocation{filename, line, 1});
        }
        if (indent != indentStack.top()) {
            auto diag = errors::inconsistentIndentation(SourceLocation{filename, line, 1});
            throw TylDiagnosticError(diag);
        }
    }
    atLineStart = false;
}

void Lexer::scanComment() {
    // Check for block comment ///
    if (peek() == '/' && peekNext() == '/') {
        advance(); // consume second /
        advance(); // consume third /
        // Block comment - scan until closing ///
        while (!isAtEnd()) {
            if (peek() == '/' && peekNext() == '/' && current + 2 < source.length() && source[current + 2] == '/') {
                advance(); advance(); advance(); // consume ///
                return;
            }
            if (peek() == '\n') {
                line++;
                column = 1;
                lineStart = current + 1;
            }
            advance();
        }
        // Unterminated block comment - just continue
        return;
    }
    // Single line comment
    while (peek() != '\n' && !isAtEnd()) advance();
}

void Lexer::scanString() {
    char quote = source[current - 1];
    std::string value;
    bool hasInterpolation = false;
    std::vector<std::pair<size_t, std::string>> interpolations;
    
    while (peek() != quote && !isAtEnd()) {
        if (peek() == '\n') {
            auto diag = errors::unterminatedString(currentLocation());
            throw TylDiagnosticError(diag);
        }
        if (peek() == '\\') {
            advance();
            switch (peek()) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '\'': value += '\''; break;
                case '{': value += '{'; break;
                default: value += peek(); break;
            }
            advance();
        } else if (peek() == '{') {
            advance();
            hasInterpolation = true;
            size_t exprStart = value.length();
            std::string expr;
            int braceDepth = 1;
            
            while (!isAtEnd() && braceDepth > 0) {
                if (peek() == '{') braceDepth++;
                else if (peek() == '}') braceDepth--;
                if (braceDepth > 0) expr += advance();
            }
            
            if (isAtEnd() && braceDepth > 0) {
                auto diag = errors::unterminatedInterpolation(currentLocation());
                throw TylDiagnosticError(diag);
            }
            advance();
            interpolations.push_back({exprStart, expr});
            value += '\x00';
        } else {
            value += advance();
        }
    }
    
    if (isAtEnd()) {
        auto diag = errors::unterminatedString(currentLocation());
        throw TylDiagnosticError(diag);
    }
    advance();
    
    if (hasInterpolation) {
        std::string encoded;
        size_t lastPos = 0;
        for (auto& [pos, expr] : interpolations) {
            for (size_t i = lastPos; i < pos && i < value.length(); i++) {
                if (value[i] != '\x00') encoded += value[i];
            }
            encoded += '\x01';
            encoded += expr;
            encoded += '\x02';
            lastPos = pos + 1;
        }
        for (size_t i = lastPos; i < value.length(); i++) {
            if (value[i] != '\x00') encoded += value[i];
        }
        addToken(TokenType::STRING, encoded);
    } else {
        addToken(TokenType::STRING, value);
    }
}

// Scan a character literal: 'A', '\n', '\u{1F600}'
void Lexer::scanChar() {
    uint32_t value = 0;
    
    if (peek() == '\\') {
        advance();  // consume backslash
        switch (peek()) {
            case 'n': value = '\n'; advance(); break;
            case 't': value = '\t'; advance(); break;
            case 'r': value = '\r'; advance(); break;
            case '\\': value = '\\'; advance(); break;
            case '\'': value = '\''; advance(); break;
            case '"': value = '"'; advance(); break;
            case '0': value = '\0'; advance(); break;
            case 'x': {
                // Hex escape: \xNN
                advance();  // consume 'x'
                std::string hex;
                for (int i = 0; i < 2 && isxdigit(peek()); i++) {
                    hex += advance();
                }
                if (hex.length() == 2) {
                    value = std::stoul(hex, nullptr, 16);
                }
                break;
            }
            case 'u': {
                // Unicode escape: \u{NNNNNN}
                advance();  // consume 'u'
                if (peek() == '{') {
                    advance();  // consume '{'
                    std::string hex;
                    while (peek() != '}' && !isAtEnd() && isxdigit(peek())) {
                        hex += advance();
                    }
                    if (peek() == '}') {
                        advance();  // consume '}'
                        if (!hex.empty() && hex.length() <= 6) {
                            value = std::stoul(hex, nullptr, 16);
                        }
                    }
                }
                break;
            }
            default:
                value = peek();
                advance();
                break;
        }
    } else if (peek() != '\'' && !isAtEnd()) {
        // Regular character - handle UTF-8
        unsigned char c = peek();
        if ((c & 0x80) == 0) {
            // ASCII
            value = c;
            advance();
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            value = (c & 0x1F) << 6;
            advance();
            if (!isAtEnd()) { value |= (peek() & 0x3F); advance(); }
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8
            value = (c & 0x0F) << 12;
            advance();
            if (!isAtEnd()) { value |= (peek() & 0x3F) << 6; advance(); }
            if (!isAtEnd()) { value |= (peek() & 0x3F); advance(); }
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8
            value = (c & 0x07) << 18;
            advance();
            if (!isAtEnd()) { value |= (peek() & 0x3F) << 12; advance(); }
            if (!isAtEnd()) { value |= (peek() & 0x3F) << 6; advance(); }
            if (!isAtEnd()) { value |= (peek() & 0x3F); advance(); }
        }
    }
    
    if (peek() != '\'') {
        auto diag = errors::unterminatedString(currentLocation());
        throw TylDiagnosticError(diag);
    }
    advance();  // consume closing quote
    
    addToken(TokenType::CHAR, (int64_t)value);
}

// Scan a byte string: b"hello" or br"\x00\xFF"
void Lexer::scanByteString(bool isRaw) {
    char quote = source[current - 1];
    std::string value;
    
    while (peek() != quote && !isAtEnd()) {
        if (peek() == '\n') {
            auto diag = errors::unterminatedString(currentLocation());
            throw TylDiagnosticError(diag);
        }
        if (!isRaw && peek() == '\\') {
            advance();
            switch (peek()) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '\'': value += '\''; break;
                case '0': value += '\0'; break;
                case 'x': {
                    // Hex escape: \xNN
                    advance();  // consume 'x'
                    std::string hex;
                    for (int i = 0; i < 2 && isxdigit(peek()); i++) {
                        hex += advance();
                    }
                    if (hex.length() == 2) {
                        value += (char)std::stoul(hex, nullptr, 16);
                    }
                    continue;  // skip the advance() at end
                }
                default: value += peek(); break;
            }
            advance();
        } else {
            value += advance();
        }
    }
    
    if (isAtEnd()) {
        auto diag = errors::unterminatedString(currentLocation());
        throw TylDiagnosticError(diag);
    }
    advance();  // consume closing quote
    
    addToken(isRaw ? TokenType::RAW_BYTE_STRING : TokenType::BYTE_STRING, value);
}


void Lexer::scanNumber() {
    while (isDigit(peek())) advance();
    
    bool isFloat = false;
    if (peek() == '.' && isDigit(peekNext())) {
        isFloat = true;
        advance();
        while (isDigit(peek())) advance();
    }
    
    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (isDigit(peek())) advance();
    }
    
    // Check for type suffix (i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, f16, f32, f64, f128)
    std::string suffix;
    size_t suffixStart = current;
    if (isAlpha(peek())) {
        while (isAlphaNumeric(peek())) {
            suffix += advance();
        }
        // Validate suffix
        static const std::set<std::string> validIntSuffixes = {"i8", "i16", "i32", "i64", "i128", "u8", "u16", "u32", "u64", "u128"};
        static const std::set<std::string> validFloatSuffixes = {"f16", "f32", "f64", "f128"};
        
        if (validIntSuffixes.count(suffix) || validFloatSuffixes.count(suffix)) {
            // Valid suffix - if it's a float suffix on an integer, treat as float
            if (validFloatSuffixes.count(suffix)) {
                isFloat = true;
            }
        } else {
            // Not a valid numeric suffix, put it back
            current = suffixStart;
            suffix.clear();
        }
    }
    
    std::string numStr = source.substr(start, current - start);
    std::string numPart = suffix.empty() ? numStr : numStr.substr(0, numStr.length() - suffix.length());
    if (isFloat) addToken(TokenType::FLOAT, std::stod(numPart));
    else addToken(TokenType::INTEGER, std::stoll(numPart));
}

void Lexer::scanIdentifier() {
    while (isAlphaNumeric(peek())) advance();
    std::string text = source.substr(start, current - start);
    
    // Check for underscore placeholder
    if (text == "_") {
        addToken(TokenType::UNDERSCORE);
        return;
    }
    
    // Check for byte string prefix: b"..." or br"..."
    if (text == "b" && (peek() == '"' || peek() == '\'')) {
        advance();  // consume the quote
        scanByteString(false);
        return;
    }
    if (text == "br" && (peek() == '"' || peek() == '\'')) {
        advance();  // consume the quote
        scanByteString(true);
        return;
    }
    
    auto it = keywords.find(text);
    if (it != keywords.end()) addToken(it->second);
    else addToken(TokenType::IDENTIFIER);
}

void Lexer::scanTemplateVar() {
    while (isAlphaNumeric(peek())) advance();
    std::string text = source.substr(start, current - start);
    addToken(TokenType::IDENTIFIER, text);
}

void Lexer::scanToken() {
    char c = advance();
    
    switch (c) {
        case '(': addToken(TokenType::LPAREN); break;
        case ')': addToken(TokenType::RPAREN); break;
        case '[': addToken(TokenType::LBRACKET); break;
        case ']': addToken(TokenType::RBRACKET); break;
        case '{': addToken(TokenType::LBRACE); break;
        case '}': addToken(TokenType::RBRACE); break;
        case ',': addToken(TokenType::COMMA); break;
        case ';': addToken(TokenType::SEMICOLON); break;
        case '%':
            if (match('=')) addToken(TokenType::PERCENT_ASSIGN);  // %= modulo assign
            else if (peek() == '%') {
                std::string opStr = "%";
                while (!isAtEnd() && peek() == '%') {
                    opStr += advance();
                }
                addToken(TokenType::CUSTOM_OP, opStr);
            } else {
                addToken(TokenType::PERCENT);
            }
            break;
        case '~': addToken(TokenType::TILDE); break;
        case '^':
            if (peek() == '^') {
                std::string opStr = "^";
                while (!isAtEnd() && peek() == '^') {
                    opStr += advance();
                }
                addToken(TokenType::CUSTOM_OP, opStr);
            } else {
                addToken(TokenType::CARET);
            }
            break;
        case '?':
            if (match('?')) addToken(TokenType::QUESTION_QUESTION);
            else if (match('.')) addToken(TokenType::QUESTION_DOT);  // ?. safe navigation
            else addToken(TokenType::QUESTION);
            break;
        case '@':
            // Attribute: @name or @name(args)
            if (isAlpha(peek())) {
                std::string attrContent;
                while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
                    attrContent += advance();
                }
                // Check for parenthesized arguments: @repr(C), @cfg(windows)
                if (!isAtEnd() && peek() == '(') {
                    attrContent += advance();  // consume '('
                    while (!isAtEnd() && peek() != ')') {
                        attrContent += advance();
                    }
                    if (!isAtEnd()) attrContent += advance();  // consume ')'
                }
                addToken(TokenType::ATTRIBUTE, attrContent);
            } else if (peek() == '@') {
                std::string opStr = "@";
                while (!isAtEnd() && peek() == '@') {
                    opStr += advance();
                }
                addToken(TokenType::CUSTOM_OP, opStr);
            } else {
                addToken(TokenType::AT);
            }
            break;
        case '$':
            if (isAlpha(peek())) scanTemplateVar();
            else addToken(TokenType::DOLLAR);
            break;
        case ':':
            if (match(':')) addToken(TokenType::DOUBLE_COLON);
            else if (match('=')) addToken(TokenType::WALRUS);  // := walrus operator
            else addToken(TokenType::COLON);
            break;
        case '+':
            if (peek() == '+') {
                std::string opStr = "+";
                while (!isAtEnd() && peek() == '+') {
                    opStr += advance();
                }
                addToken(TokenType::CUSTOM_OP, opStr);
            } else {
                addToken(match('=') ? TokenType::PLUS_ASSIGN : TokenType::PLUS);
            }
            break;
        case '-':
            if (match('>')) addToken(TokenType::ARROW);
            else if (peek() == '-') {
                std::string opStr = "-";
                while (!isAtEnd() && peek() == '-') {
                    opStr += advance();
                }
                addToken(TokenType::CUSTOM_OP, opStr);
            } else {
                addToken(match('=') ? TokenType::MINUS_ASSIGN : TokenType::MINUS);
            }
            break;
        case '*':
            if (match('*')) {
                // Check for more operator chars to form custom op like *** or **=
                std::string opStr = "**";
                while (!isAtEnd() && isOperatorChar(peek())) {
                    opStr += advance();
                }
                addToken(TokenType::CUSTOM_OP, opStr);
            } else {
                addToken(match('=') ? TokenType::STAR_ASSIGN : TokenType::STAR);
            }
            break;
        case '/':
            if (match('/')) scanComment();
            else if (match('=')) addToken(TokenType::SLASH_ASSIGN);
            else addToken(TokenType::SLASH);
            break;
        case '.':
            if (match('.')) {
                addToken(TokenType::DOTDOT);  // .. range (inclusive)
            }
            else addToken(TokenType::DOT);
            break;
        case '=':
            if (match('=')) addToken(TokenType::EQ);
            else if (match('>')) addToken(TokenType::DOUBLE_ARROW);
            else addToken(TokenType::ASSIGN);
            break;
        case '!':
            if (match('=')) addToken(TokenType::NE);
            else addToken(TokenType::BANG);
            break;
        case '<':
            if (match('-')) addToken(TokenType::CHAN_SEND);  // <- for channel send/receive
            else if (match('=')) {
                if (match('>')) addToken(TokenType::SPACESHIP);
                else addToken(TokenType::LE);
            } else addToken(TokenType::LT);
            break;
        case '>':
            addToken(match('=') ? TokenType::GE : TokenType::GT);
            break;
        case '&':
            if (match('&')) addToken(TokenType::AMP_AMP);
            else addToken(TokenType::AMP);
            break;
        case '|':
            if (match('|')) addToken(TokenType::PIPE_PIPE);
            else if (match('>')) addToken(TokenType::PIPE_GT);
            else addToken(TokenType::PIPE);
            break;
        case ' ': case '\t': case '\r': break;
        case '\n':
            if (!tokens.empty() && tokens.back().type != TokenType::NEWLINE &&
                tokens.back().type != TokenType::INDENT) {
                addToken(TokenType::NEWLINE);
            }
            atLineStart = true;
            break;
        case '"': case '\'':
            // Check for byte string prefix (b" or b')
            // Note: This case handles regular strings/chars, byte strings are handled in identifier scanning
            if (source[current - 1] == '\'') {
                // Check if this is a lifetime annotation: 'a, 'static, etc.
                // Lifetimes are ' followed by an identifier
                if (isAlpha(peek())) {
                    // This is a lifetime annotation
                    std::string lifetime = "'";
                    while (isAlphaNumeric(peek())) {
                        lifetime += advance();
                    }
                    addToken(TokenType::LIFETIME, lifetime);
                } else {
                    scanChar();
                }
            } else {
                scanString();
            }
            break;
        case '#':
            // Check for attribute: #[...] (legacy/Rust-style, still supported)
            if (peek() == '[') {
                advance();  // consume '['
                std::string attrContent;
                while (!isAtEnd() && peek() != ']') {
                    attrContent += advance();
                }
                if (!isAtEnd()) advance();  // consume ']'
                addToken(TokenType::ATTRIBUTE, attrContent);
            } else {
                // Single-line comment (Python style)
                scanComment();
            }
            break;
        default:
            if (isDigit(c)) scanNumber();
            else if (isAlpha(c)) scanIdentifier();
            else {
                auto diag = errors::unexpectedChar(c, currentLocation());
                throw TylDiagnosticError(diag);
            }
            break;
    }
}

std::vector<Token> Lexer::tokenize() {
    while (!isAtEnd()) {
        if (atLineStart) {
            handleIndentation();
            if (isAtEnd()) break;
        }
        start = current;
        scanToken();
    }
    
    while (indentStack.size() > 1) {
        indentStack.pop();
        tokens.emplace_back(TokenType::DEDENT, "", SourceLocation{filename, line, column});
    }
    
    tokens.emplace_back(TokenType::END_OF_FILE, "", SourceLocation{filename, line, column});
    return tokens;
}

} // namespace tyl
