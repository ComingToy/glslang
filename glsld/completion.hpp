#ifndef __GLSLD_COMPLETION_H__
#define __GLSLD_COMPLETION_H__

#include "doc.hpp"
struct CompletionResult {
    std::string label;
    int kind;
    std::string detail;
    std::string documentation;
};

extern std::vector<CompletionResult> completion(Doc& doc, std::string const& input);
#endif
