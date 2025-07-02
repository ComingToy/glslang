#ifndef __GLSLD_PARSER_H__
#define __GLSLD_PARSER_H__
#include "StandAlone/DirStackFileIncluder.h"
#include "glslang/MachineIndependent/ScanContext.h"
#include "glslang/MachineIndependent/glslang_tab.cpp.h"
#include "glslang/MachineIndependent/preprocessor/PpContext.h"
#include <memory>

struct ParserResouce {
    glslang::TSymbolTable* symbol_table;
    glslang::TIntermediate* intermediate;
    glslang::TParseContext* parse_context;
    glslang::TScanContext* scan_context;
    glslang::TPpContext* ppcontext;
    DirStackFileIncluder* includer;

	~ParserResouce()
	{
		delete parse_context;
		delete ppcontext;
		delete scan_context;
		delete intermediate;
		delete symbol_table;
		delete includer;
	}
};

extern std::unique_ptr<ParserResouce> create_parser(const int version, EProfile profile, EShLanguage stage,
                                                    glslang::SpvVersion spvVersion, const char* entrypoint);
#endif
