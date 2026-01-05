// Tyl Compiler - Diagnostic System
// Rust/Clang-style error reporting with source context and suggestions
#ifndef TYL_DIAGNOSTICS_H
#define TYL_DIAGNOSTICS_H

#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <memory>

namespace tyl {

// Error severity levels
enum class DiagnosticLevel {
    Note,       // Additional context
    Warning,    // Non-fatal issue
    Error,      // Compilation error
    Fatal       // Unrecoverable error
};

// Error categories for organization
enum class DiagnosticCategory {
    Lexer,      // Tokenization errors
    Parser,     // Syntax errors
    Type,       // Type checking errors
    Semantic,   // Other semantic errors
    Codegen,    // Code generation errors
    Linker,     // Linking errors
    Runtime,    // Runtime errors
    IO          // File/IO errors
};

// Source span for multi-character highlighting
struct SourceSpan {
    std::string filename;
    int startLine = 1;
    int startColumn = 1;
    int endLine = 1;
    int endColumn = 1;
    
    static SourceSpan fromLocation(const std::string& file, int line, int col, int len = 1) {
        return {file, line, col, line, col + len};
    }
};

// A single diagnostic message
struct Diagnostic {
    DiagnosticLevel level = DiagnosticLevel::Error;
    DiagnosticCategory category = DiagnosticCategory::Parser;
    std::string code;           // e.g., "E0001"
    std::string message;
    SourceSpan span;
    std::string suggestion;     // Optional fix suggestion
    std::string hint;           // Additional help text
    std::vector<Diagnostic> notes;  // Related notes
    
    // Builder pattern for easy construction
    Diagnostic& withCode(const std::string& c) { code = c; return *this; }
    Diagnostic& withSuggestion(const std::string& s) { suggestion = s; return *this; }
    Diagnostic& withHint(const std::string& h) { hint = h; return *this; }
    Diagnostic& addNote(const Diagnostic& n) { notes.push_back(n); return *this; }
};

// Source file cache for displaying context
class SourceCache {
public:
    static SourceCache& instance() {
        static SourceCache cache;
        return cache;
    }
    
    void loadFile(const std::string& filename) {
        if (files_.count(filename)) return;
        
        std::ifstream file(filename);
        if (!file) return;
        
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        files_[filename] = std::move(lines);
    }
    
    void cacheSource(const std::string& filename, const std::string& source) {
        std::vector<std::string> lines;
        std::istringstream stream(source);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
        files_[filename] = std::move(lines);
    }
    
    std::optional<std::string> getLine(const std::string& filename, int lineNum) {
        auto it = files_.find(filename);
        if (it == files_.end()) {
            loadFile(filename);
            it = files_.find(filename);
            if (it == files_.end()) return std::nullopt;
        }
        
        if (lineNum < 1 || lineNum > (int)it->second.size()) {
            return std::nullopt;
        }
        return it->second[lineNum - 1];
    }
    
private:
    std::unordered_map<std::string, std::vector<std::string>> files_;
};


// ANSI color codes for terminal output
namespace colors {
    inline bool enabled = true;
    
    inline const char* reset()   { return enabled ? "\033[0m" : ""; }
    inline const char* bold()    { return enabled ? "\033[1m" : ""; }
    inline const char* red()     { return enabled ? "\033[31m" : ""; }
    inline const char* green()   { return enabled ? "\033[32m" : ""; }
    inline const char* yellow()  { return enabled ? "\033[33m" : ""; }
    inline const char* blue()    { return enabled ? "\033[34m" : ""; }
    inline const char* magenta() { return enabled ? "\033[35m" : ""; }
    inline const char* cyan()    { return enabled ? "\033[36m" : ""; }
}

// Diagnostic renderer - formats and prints diagnostics
class DiagnosticRenderer {
public:
    void render(const Diagnostic& diag, std::ostream& out = std::cerr) {
        renderHeader(diag, out);
        renderSourceContext(diag, out);
        renderSuggestion(diag, out);
        renderHint(diag, out);
        
        for (const auto& note : diag.notes) {
            render(note, out);
        }
        out << "\n";
    }
    
private:
    void renderHeader(const Diagnostic& diag, std::ostream& out) {
        // Level color and text
        const char* levelColor = colors::red();
        const char* levelText = "error";
        
        switch (diag.level) {
            case DiagnosticLevel::Note:
                levelColor = colors::cyan();
                levelText = "note";
                break;
            case DiagnosticLevel::Warning:
                levelColor = colors::yellow();
                levelText = "warning";
                break;
            case DiagnosticLevel::Error:
                levelColor = colors::red();
                levelText = "error";
                break;
            case DiagnosticLevel::Fatal:
                levelColor = colors::red();
                levelText = "fatal error";
                break;
        }
        
        out << colors::bold() << levelColor << levelText << colors::reset();
        
        if (!diag.code.empty()) {
            out << "[" << diag.code << "]";
        }
        
        out << colors::bold() << ": " << diag.message << colors::reset() << "\n";
        
        // Location
        out << "  " << colors::blue() << "-->" << colors::reset() << " "
            << diag.span.filename << ":" << diag.span.startLine << ":" << diag.span.startColumn << "\n";
    }
    
    void renderSourceContext(const Diagnostic& diag, std::ostream& out) {
        auto sourceLine = SourceCache::instance().getLine(diag.span.filename, diag.span.startLine);
        if (!sourceLine) return;
        
        std::string lineNumStr = std::to_string(diag.span.startLine);
        std::string padding(lineNumStr.size(), ' ');
        
        // Empty line before source
        out << "   " << colors::blue() << padding << " |" << colors::reset() << "\n";
        
        // Source line
        out << "   " << colors::blue() << lineNumStr << " | " << colors::reset() << *sourceLine << "\n";
        
        // Underline/caret
        out << "   " << colors::blue() << padding << " | " << colors::reset();
        
        // Spaces before the caret
        int caretStart = diag.span.startColumn - 1;
        for (int i = 0; i < caretStart && i < (int)sourceLine->size(); i++) {
            out << ((*sourceLine)[i] == '\t' ? '\t' : ' ');
        }
        
        // The caret/underline
        const char* caretColor = (diag.level == DiagnosticLevel::Error || diag.level == DiagnosticLevel::Fatal) 
                                  ? colors::red() : colors::yellow();
        out << colors::bold() << caretColor;
        
        int underlineLen = diag.span.endColumn - diag.span.startColumn;
        if (underlineLen <= 1) {
            out << "^";
        } else {
            for (int i = 0; i < underlineLen; i++) {
                out << "~";
            }
        }
        out << colors::reset() << "\n";
    }
    
    void renderSuggestion(const Diagnostic& diag, std::ostream& out) {
        if (diag.suggestion.empty()) return;
        
        out << "   " << colors::green() << "suggestion" << colors::reset() 
            << ": " << diag.suggestion << "\n";
    }
    
    void renderHint(const Diagnostic& diag, std::ostream& out) {
        if (diag.hint.empty()) return;
        
        out << "   " << colors::cyan() << "help" << colors::reset() 
            << ": " << diag.hint << "\n";
    }
};

// Diagnostic collector - accumulates errors and warnings
class DiagnosticCollector {
public:
    void add(Diagnostic diag) {
        if (diag.level == DiagnosticLevel::Error || diag.level == DiagnosticLevel::Fatal) {
            errorCount_++;
        } else if (diag.level == DiagnosticLevel::Warning) {
            warningCount_++;
        }
        diagnostics_.push_back(std::move(diag));
    }
    
    void error(DiagnosticCategory cat, const std::string& msg, const SourceSpan& span) {
        Diagnostic d;
        d.level = DiagnosticLevel::Error;
        d.category = cat;
        d.message = msg;
        d.span = span;
        add(std::move(d));
    }
    
    void warning(DiagnosticCategory cat, const std::string& msg, const SourceSpan& span) {
        Diagnostic d;
        d.level = DiagnosticLevel::Warning;
        d.category = cat;
        d.message = msg;
        d.span = span;
        add(std::move(d));
    }
    
    bool hasErrors() const { return errorCount_ > 0; }
    int errorCount() const { return errorCount_; }
    int warningCount() const { return warningCount_; }
    
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    
    void render(std::ostream& out = std::cerr) {
        DiagnosticRenderer renderer;
        for (const auto& diag : diagnostics_) {
            renderer.render(diag, out);
        }
        
        // Summary
        if (errorCount_ > 0 || warningCount_ > 0) {
            out << colors::bold();
            if (errorCount_ > 0) {
                out << colors::red() << "error" << colors::reset() << colors::bold()
                    << ": could not compile due to " << errorCount_ << " error(s)";
            }
            if (warningCount_ > 0) {
                if (errorCount_ > 0) out << ", ";
                out << warningCount_ << " warning(s)";
            }
            out << colors::reset() << "\n";
        }
    }
    
    void clear() {
        diagnostics_.clear();
        errorCount_ = 0;
        warningCount_ = 0;
    }
    
private:
    std::vector<Diagnostic> diagnostics_;
    int errorCount_ = 0;
    int warningCount_ = 0;
};

// Global diagnostic collector
inline DiagnosticCollector& diagnostics() {
    static DiagnosticCollector collector;
    return collector;
}

} // namespace tyl

#endif // TYL_DIAGNOSTICS_H
