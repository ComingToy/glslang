#ifndef __GLSLD_PARSER_H__
#define __GLSLD_PARSER_H__
#include "StandAlone/DirStackFileIncluder.h"
#include "glslang/MachineIndependent/ScanContext.h"
#include "glslang/MachineIndependent/glslang_tab.cpp.h"
#include "glslang/MachineIndependent/preprocessor/PpContext.h"
#include <memory>

struct ParserResouce {
    std::unique_ptr<glslang::TSymbolTable> symbol_table;
    std::unique_ptr<glslang::TIntermediate> intermediate;
    std::unique_ptr<glslang::TParseContext> parse_context;
    std::unique_ptr<glslang::TScanContext> scan_context;
    std::unique_ptr<glslang::TPpContext> ppcontext;
    std::unique_ptr<DirStackFileIncluder> includer;

    ParserResouce(std::unique_ptr<glslang::TSymbolTable>&& symbol_table,
                  std::unique_ptr<glslang::TIntermediate>&& intermediate,
                  std::unique_ptr<glslang::TParseContext>&& parse_context,
                  std::unique_ptr<glslang::TScanContext>&& scan_context,
                  std::unique_ptr<glslang::TPpContext>&& ppcontext, std::unique_ptr<DirStackFileIncluder>&& includer)
    {
        this->symbol_table = std::move(symbol_table);
        this->intermediate = std::move(intermediate);
        this->parse_context = std::move(parse_context);
        this->scan_context = std::move(scan_context);
        this->ppcontext = std::move(ppcontext);
        this->includer = std::move(includer);
    }
};

extern std::unique_ptr<ParserResouce> create_parser(const int version, EProfile profile, EShLanguage stage,
                                                    glslang::SpvVersion spvVersion, const char* entrypoint);
#endif
