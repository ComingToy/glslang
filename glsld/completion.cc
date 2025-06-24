#include "completion.hpp"
#include "../glslang/MachineIndependent/Scan.h"
#include "StandAlone/DirStackFileIncluder.h"
#include "glslang/MachineIndependent/Initialize.h"
#include "glslang/MachineIndependent/ScanContext.h"
#include "glslang/MachineIndependent/glslang_tab.cpp.h"
#include "glslang/MachineIndependent/preprocessor/PpContext.h"
#include <iostream>
#include <memory>
#include <stack>
#include <tuple>
#include <vector>

extern int yylex(YYSTYPE*, glslang::TParseContext&);
static glslang::TParseContext* CreateParseContext(glslang::TSymbolTable& symbolTable,
                                                  glslang::TIntermediate& intermediate, int version, EProfile profile,
                                                  glslang::EShSource source, EShLanguage language, TInfoSink& infoSink,
                                                  glslang::SpvVersion spvVersion, bool forwardCompatible,
                                                  EShMessages messages, bool parsingBuiltIns,
                                                  std::string sourceEntryPointName = "")
{
    switch (source) {
    case glslang::EShSourceGlsl: {
        if (sourceEntryPointName.size() == 0)
            intermediate.setEntryPointName("main");
        glslang::TString entryPoint = sourceEntryPointName.c_str();
        return new glslang::TParseContext(symbolTable, intermediate, parsingBuiltIns, version, profile, spvVersion,
                                          language, infoSink, forwardCompatible, messages, &entryPoint);
    }

    default:
        infoSink.info.message(glslang::EPrefixInternalError, "Unable to determine source language");
        return nullptr;
    }
}

bool InitializeSymbolTable(const glslang::TString& builtIns, int version, EProfile profile,
                           const glslang::SpvVersion& spvVersion, EShLanguage language, glslang::EShSource source,
                           TInfoSink& infoSink, glslang::TSymbolTable& symbolTable)
{
    glslang::TIntermediate intermediate(language, version, profile);

    intermediate.setSource(source);

    std::unique_ptr<glslang::TParseContextBase> parseContext(CreateParseContext(symbolTable, intermediate, version,
                                                                                profile, source, language, infoSink,
                                                                                spvVersion, true, EShMsgDefault, true));

    glslang::TShader::ForbidIncluder includer;
    glslang::TPpContext ppContext(*parseContext, "", includer);
    glslang::TScanContext scanContext(*parseContext);
    parseContext->setScanContext(&scanContext);
    parseContext->setPpContext(&ppContext);

    //
    // Push the symbol table to give it an initial scope.  This
    // push should not have a corresponding pop, so that built-ins
    // are preserved, and the test for an empty table fails.
    //

    symbolTable.push();

    const char* builtInShaders[2];
    size_t builtInLengths[2];
    builtInShaders[0] = builtIns.c_str();
    builtInLengths[0] = builtIns.size();

    if (builtInLengths[0] == 0)
        return true;

    glslang::TInputScanner input(1, builtInShaders, builtInLengths);
    if (!parseContext->parseShaderStrings(ppContext, input) != 0) {
        infoSink.info.message(glslang::EPrefixInternalError, "Unable to parse built-ins");
        fprintf(stderr, "Unable to parse built-ins\n%s\n", infoSink.info.c_str());
        fprintf(stderr, "%s\n", builtInShaders[0]);

        return false;
    }

    return true;
}

static bool AddContextSpecificSymbols(const TBuiltInResource* resources, TInfoSink& infoSink,
                                      glslang::TSymbolTable& symbolTable, int version, EProfile profile,
                                      const glslang::SpvVersion& spvVersion, EShLanguage language,
                                      glslang::EShSource source)
{
    std::unique_ptr<glslang::TBuiltInParseables> builtInParseables(new glslang::TBuiltIns());

    if (builtInParseables == nullptr)
        return false;

    builtInParseables->initialize(*resources, version, profile, spvVersion, language);
    if (!InitializeSymbolTable(builtInParseables->getCommonString(), version, profile, spvVersion, language, source,
                               infoSink, symbolTable))
        return false;
    builtInParseables->identifyBuiltIns(version, profile, spvVersion, language, symbolTable, *resources);

    return true;
}

std::vector<CompletionResult> completion_variable(Doc& doc, std::string const& prefix)
{
    auto syms = doc.lookup_symbols_by_prefix(prefix);
}

std::vector<CompletionResult> completion(Doc& doc, std::string const& input)
{
    const char* source = input.data();
    size_t len = input.size();
    glslang::TInputScanner userInput(1, &source, &len);

    auto version = doc.intermediate()->getVersion();
    auto profile = doc.intermediate()->getProfile();
    auto stage = doc.intermediate()->getStage();
    auto spvVersion = doc.intermediate()->getSpv();
    auto entrypoint = doc.intermediate()->getEntryPointName();

    std::unique_ptr<glslang::TSymbolTable> symbolTable(new glslang::TSymbolTable);
    TInfoSink infoSink;
    AddContextSpecificSymbols(&Doc::kDefaultTBuiltInResource, infoSink, *symbolTable, version, profile, spvVersion,
                              doc.language(), glslang::EShSourceGlsl);

    auto intermediate = std::make_unique<glslang::TIntermediate>(doc.language());
    const EShMessages message = static_cast<EShMessages>(EShMsgCascadingErrors | EShMsgSpvRules | EShMsgVulkanRules);

    std::unique_ptr<glslang::TParseContext> parseContext(
        CreateParseContext(*symbolTable, *intermediate, version, profile, glslang::EShSourceGlsl, stage, infoSink,
                           spvVersion, false, message, false, entrypoint));
    parseContext->compileOnly = false;

    glslang::TScanContext scanContext(*parseContext);
    parseContext->setScanContext(&scanContext);

    DirStackFileIncluder includer;

    glslang::TPpContext ppContext(*parseContext, "", includer);
    ppContext.setInput(userInput, false);
    parseContext->setPpContext(&ppContext);
    parseContext->setScanner(&userInput);

    std::vector<std::tuple<YYSTYPE, int>> input_toks;
    while (true) {
        YYSTYPE stype;
        int tok = yylex(&stype, *parseContext);
        if (!tok) {
            break;
        }

        input_toks.push_back(std::make_tuple(stype, tok));
    }

    if (input_toks.empty())
        return {};

    auto& [stype, tok] = input_toks.front();
    if (tok != IDENTIFIER) {
        return {};
    }

    std::vector<CompletionResult> results;
    if (input_toks.size() == 1) {
        for (auto& sym : doc.lookup_symbols_by_prefix(stype.lex.string->c_str())) {
            auto const& ty = sym->getType();
            auto ftypename = ty.getCompleteString(true, false, false);

            CompletionResult r = {sym->getName().c_str(), 6, ftypename.c_str(), ""};
            results.push_back(r);
        }
        return results;
    }

    auto* base_sym = doc.lookup_symbol_by_name(stype.lex.string->c_str());
    const glslang::TType* type = &base_sym->getType();

    int i = 1;
    int state = 0; // no left bracket
    while (i < input_toks.size()) {
        auto& [stype, tok] = input_toks[i];
        if (tok == DOT && !type->isStruct()) {
            return {};
        }

        if (tok == LEFT_BRACKET) {
            if (state) {
                return {};
            }

            if (!type->isArray())
                return {};
        }
    }
}
