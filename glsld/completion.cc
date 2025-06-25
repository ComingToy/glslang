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

struct InputStackState {
    int kind; // 0 for lex. 1 for struct 2 for arr 3 scalar
    const glslang::TType* ttype;
    const YYSTYPE* stype;
    int tok;
};

glslang::TIntermSymbol* reduce_symbol_(Doc& doc, YYSTYPE const& stype)
{
    return doc.lookup_symbol_by_name(stype.lex.string->c_str());
}

bool reduce_field_(Doc& doc, std::stack<InputStackState>& input_stack)
{
    auto field = input_stack.top();
    input_stack.pop();
    auto dot = input_stack.top();
    input_stack.pop();
    auto s = input_stack.top();
    input_stack.pop();

    auto& members = *s.ttype->getStruct();
    int i = 0;
    int kind = 0;

    glslang::TType* fty = nullptr;

    if (field.kind != 0 || field.tok != IDENTIFIER) {
        return false;
    }

    if (dot.kind != 0 || dot.tok != DOT) {
        return false;
    }

    if (s.kind != 1 || !s.ttype->isStruct()) {
        return false;
    }

    for (i = 0; i < members.size(); ++i) {
        fty = members[i].type;
        if (fty->isReference()) {
            fty = fty->getReferentType();
        }

        if (fty->getFieldName() == field.stype->lex.string->c_str()) {
            break;
        }
    }

    if (!fty) {
        return false;
    }

    if (fty->isArray()) {
        kind = 2;
    } else if (fty->isStruct()) {
        kind = 1;
    } else if (fty->isScalar()) {
        kind = 3;
    }

    input_stack.push({kind, fty, nullptr, 0});
    return true;
}

bool reduce_subscript_(Doc& doc, std::stack<InputStackState>& input_stack)
{
    while (!input_stack.empty() || input_stack.top().tok != LEFT_BRACKET) {
        input_stack.pop();
    }

    if (input_stack.empty()) {
        return false;
    }

    input_stack.pop();
    auto& top = input_stack.top();
    if (top.kind != 2) {
        return false;
    }

    auto* ttype = top.ttype;
    auto array = ttype->getArraySizes();
    auto node = array->getDimNode(0);
    if (node->isArray()) {
        top.kind = 2;
    } else if (node->isStruct()) {
        top.kind = 1;
    } else {
        top.kind = 3;
    }

    top.ttype = &node->getType();
    return true;
}

bool reduce_arr_(Doc& doc, std::stack<InputStackState>& input_stack)
{
    auto& top = input_stack.top();
    if (top.kind == 2) {
        return true;
    }

    if (top.kind != 0 || top.tok != IDENTIFIER) {
        return false;
    }

    auto* sym = doc.lookup_symbol_by_name(top.stype->lex.string->c_str());
    if (!sym) {
        return false;
    }

    const auto* type = &sym->getType();
    if (type->isReference()) {
        type = type->getReferentType();
    }

    if (!type->isArray()) {
        return false;
    }

    top.kind = 2;
    top.ttype = type;
    return true;
}

bool reduce_struct_(Doc& doc, std::stack<InputStackState>& input_stack)
{
    auto& top = input_stack.top();

    // not lex tok
    if (top.kind != 0) {
        return false;
    }

    if (top.kind == 1)
        return true;

    auto tok = top.tok;
    if (tok != IDENTIFIER) {
        return false;
    }

    auto* sym = doc.lookup_symbol_by_name(top.stype->lex.string->c_str());
    if (!sym) {
        return false;
    }

    const auto* type = &sym->getType();
    if (type->isReference()) {
        type = type->getReferentType();
    }

    if (!type->isStruct()) {
        return false;
    }

    top.kind = 1;
    top.ttype = type;
    return true;
}

bool complete_by_prefix(Doc& doc, const std::stack<InputStackState>& input_stack, std::string const& prefix,
                        std::vector<CompletionResult>& results)
{
    if (input_stack.empty()) {
        auto symbols = doc.lookup_symbols_by_prefix(prefix);
        if (symbols.empty()) {
            return false;
        }

        for (auto const& sym : symbols) {
            auto const* type = &sym->getType();
            if (type->isReference()) {
                type = type->getReferentType();
            }

            auto detail = type->getCompleteString(true, false, false);
            results.push_back({sym->getName().c_str(), CompletionItemKind::Variable, detail.c_str(), ""});
        }
    } else {
        auto& state = input_stack.top();
        const auto* type = state.ttype;
        if (type->isReference()) {
            type = type->getReferentType();
        }

        if (!type->isStruct()) {
            std::cerr << "complete prefix must followed struct variable." << std::endl;
            return false;
        }

        auto match_prefix = [&prefix](std::string const& field) {
            if (prefix.empty())
                return true;

            return prefix == field.substr(0, prefix.size());
        };

        const auto* members = type->getStruct();
        for (int i = 0; i < members->size(); ++i) {
            const auto* field = (*members)[i].type;
            auto label = field->getFieldName();
            if (!match_prefix(label.c_str())) {
                continue;
            }

            auto detail = field->getCompleteString(true, false, false);
            results.push_back({label.c_str(), CompletionItemKind::Field, detail.c_str(), ""});
        }
    }

    return true;
}

void do_complete_exp_(Doc& doc, std::stack<InputStackState>& input_stack, std::vector<CompletionResult>& results)
{
    if (input_stack.top().kind != 0) {
        return;
    }

    auto input = input_stack.top();
    input_stack.pop();

    if (input.tok == IDENTIFIER) {
        auto top = input_stack.top();
        if (top.kind != 0) {
            auto symbols = doc.lookup_symbols_by_prefix(input.stype->lex.string->c_str());
            for (auto* sym : symbols) {
                CompletionResult r = {sym->getName().c_str(), CompletionItemKind::Variable,
                                      sym->getType().getCompleteString(true, false, false).c_str(), ""};
                results.emplace_back(r);
            }
        } else if (top.tok == DOT) {
            input_stack.pop();
            if (input_stack.top().kind != 1) {
            }
        }
    }
}

std::vector<CompletionResult> do_complete(Doc& doc, std::vector<std::tuple<YYSTYPE, int>> const& lex_info)
{
    //very tiny varaible exp parser
    static std::vector<std::vector<int>> completion_rules = {
        {IDENTIFIER},
        {IDENTIFIER, DOT},
        {IDENTIFIER, DOT, IDENTIFIER},
        {IDENTIFIER, LEFT_BRACKET, INTCONSTANT, RIGHT_BRACKET},
        {IDENTIFIER, LEFT_BRACKET, IDENTIFIER, RIGHT_BRACKET},
    };

    std::vector<CompletionResult> results;
    std::stack<InputStackState> input_stack;

#define START 0
#define END -1
#define EXPECT_DOT_LBRACKET 1
#define EXPECT_IDENTIFIER 2
#define EXPECT_RBRACKET 3

    int state = START;
    //0 tok for start, -1 tok for end
    for (int i = 0; i < lex_info.size(); ++i) {
        auto const& [stype, tok] = lex_info[i];
        if (tok == -1) {
            //do complete at end
            if (input_stack.top().kind == 0) {
                do_complete_exp_(doc, input_stack, results);
            }

            return results;
        }

        switch (state) {
        case START:
            if (tok == IDENTIFIER) {
                input_stack.push({0, nullptr, &stype, tok});
                state = EXPECT_DOT_LBRACKET;
            } else {
                // err
                return results;
            }
            break;
        case EXPECT_DOT_LBRACKET:
            if (tok == DOT) {
                if (!reduce_struct_(doc, input_stack)) {
                    return results;
                }

                input_stack.push({0, nullptr, &stype, tok});
                state = EXPECT_IDENTIFIER;
                break;
            } else if (tok == LEFT_BRACKET) {
                if (!reduce_arr_(doc, input_stack)) {
                    return results;
                }
                input_stack.push({0, nullptr, &stype, tok});
                state = EXPECT_RBRACKET;
            } else {
                return results;
            }
            break;
        case EXPECT_IDENTIFIER: {
            auto const& [nstype, ntok] = lex_info[i + 1];
            if (ntok == -1) {
                input_stack.push({0, nullptr, &stype, tok});
            } else if (tok == IDENTIFIER) {
                if (input_stack.top().kind == 0 && input_stack.top().tok == DOT) {
                    input_stack.push({0, nullptr, &stype, tok});
                    if (!reduce_field_(doc, input_stack)) {
                        return results;
                    }
                    state = EXPECT_DOT_LBRACKET;
                }
            } else {
                return results;
            }
        } break;
        case EXPECT_RBRACKET:
            if (tok == RIGHT_BRACKET) {
                if (!reduce_subscript_(doc, input_stack)) {
                    return results;
                }
                state = EXPECT_DOT_LBRACKET;
            }
            break;
        default:
            break;
        }
    }

    return results;
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

            CompletionResult r = {sym->getName().c_str(), CompletionItemKind::Field, ftypename.c_str(), ""};
            results.push_back(r);
        }
        return results;
    }

    input_toks.push_back({YYSTYPE{}, -1});
}
