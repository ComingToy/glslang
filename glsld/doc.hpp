#ifndef __GLSLD_DOC_HPP__
#define __GLSLD_DOC_HPP__
#include "../glslang/MachineIndependent/localintermediate.h"
#include "../glslang/Public/ShaderLang.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class Doc {
    struct __Resource {
        std::string uri;
        int version;
        std::string text_;
        std::vector<std::string> lines_;
        EShLanguage language;
        std::unique_ptr<glslang::TShader> shader;
        std::map<int, std::vector<TIntermNode*>> nodes_by_line;
        std::map<long long, glslang::TIntermSymbol*> defs;
        std::vector<glslang::TIntermSymbol*> symbols;
        int ref = 1;
    };

    __Resource* resource_;
    void infer_language_();
    void release_();

public:
    Doc();
    Doc(std::string const& uri, const int version, std::string const& text);
    Doc(const Doc& rhs);
    Doc(Doc&& rhs);
    Doc& operator=(const Doc& doc);
    Doc& operator=(Doc&& doc);
    virtual ~Doc();

    bool parse(std::vector<std::string> const& include_dirs);

    int version() const { return resource_->version; }
    std::vector<std::string> const& lines() const { return resource_->lines_; }
    std::string const& uri() const { return resource_->uri; }
    EShLanguage language() const { return resource_->language; }
    void set_version(const int version) { resource_->version = version; }
    void set_text(std::string const& text);
    void set_uri(std::string const& uri) { resource_->uri = uri; }
    std::vector<glslang::TIntermSymbol*>& symbols() { return resource_->symbols; }
    std::vector<glslang::TIntermSymbol*> lookup_symbols_by_prefix(std::string const& prefix);
    const char* info_log() { return resource_->shader->getInfoLog(); }

    struct LookupResult {
        enum class Kind { SYMBOL, FIELD, ERROR } kind;
        union {
            glslang::TIntermSymbol* sym;
            glslang::TTypeLoc field;
        };
    };

    std::vector<LookupResult> lookup_nodes_at(const int line, const int col);
    glslang::TSourceLoc locate_symbol_def(glslang::TIntermSymbol* use);
};
#endif
