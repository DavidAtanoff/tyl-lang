// Tyl Compiler - Lexer
#ifndef TYL_LEXER_H
#define TYL_LEXER_H

#include "frontend/token/token.h"
#include <stack>

namespace tyl {

class Lexer {
public:
    Lexer(const std::string& source, const std::string& filename = "<input>");
    std::vector<Token> tokenize();
    
private:
    std::string source;
    std::string filename;
    size_t start = 0;
    size_t current = 0;
    int line = 1;
    int column = 1;
    int lineStart = 0;
    
    std::vector<Token> tokens;
    std::stack<int> indentStack;
    bool atLineStart = true;
    
    static const std::unordered_map<std::string, TokenType> keywords;
    
    bool isAtEnd() const { return current >= source.length(); }
    char peek() const { return isAtEnd() ? '\0' : source[current]; }
    char peekNext() const { return current + 1 >= source.length() ? '\0' : source[current + 1]; }
    char advance();
    bool match(char expected);
    
    void skipWhitespace();
    void handleIndentation();
    void scanToken();
    
    void addToken(TokenType type);
    void addToken(TokenType type, int64_t value);
    void addToken(TokenType type, double value);
    void addToken(TokenType type, const std::string& value);
    
    void scanString();
    void scanChar();
    void scanByteString(bool isRaw);
    void scanNumber();
    void scanIdentifier();
    void scanTemplateVar();
    void scanComment();
    
    SourceLocation currentLocation() const { return {filename, line, column}; }
    
    bool isDigit(char c) const { return c >= '0' && c <= '9'; }
    bool isAlpha(char c) const { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    bool isAlphaNumeric(char c) const { return isAlpha(c) || isDigit(c); }
    bool isOperatorChar(char c) const { 
        return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || 
               c == '<' || c == '>' || c == '=' || c == '!' || c == '&' || 
               c == '|' || c == '^' || c == '~' || c == '@' || c == '#';
    }
};

} // namespace tyl

#endif // TYL_LEXER_H
