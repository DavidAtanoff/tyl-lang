// Tyl Compiler - Common definitions
#ifndef TYL_COMMON_H
#define TYL_COMMON_H

// Platform compatibility - must be first
#include "common/platform.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>
#include <optional>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <functional>
#include <cstdint>
#include <map>

namespace tyl {

// Source location for error reporting
struct SourceLocation {
    std::string filename;
    int line = 1;
    int column = 1;
    
    std::string toString() const {
        return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
    }
};

// Compiler error
class TylError : public std::runtime_error {
public:
    SourceLocation location;
    
    TylError(const std::string& msg, SourceLocation loc = {})
        : std::runtime_error(loc.toString() + ": " + msg), location(loc) {}
};

// Forward declarations
class Token;
struct ASTNode;
struct Type;
struct Value;

} // namespace tyl

#endif // TYL_COMMON_H
