#ifndef __GLSLD_COMPLETION_H__
#define __GLSLD_COMPLETION_H__

#include "doc.hpp"
enum class CompletionItemKind {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25,
};

struct CompletionResult {
    std::string label;
    CompletionItemKind kind;
    std::string detail;
    std::string documentation;
	std::string insert_text;
};

extern std::vector<CompletionResult> completion(Doc& doc, std::string const& input, const int line, const int col);
#endif
