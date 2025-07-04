#include "completion.hpp"
#include "parser.hpp"
#include <cstdio>
#include <memory>
#include <stack>
#include <tuple>
#include <vector>

extern int yylex(YYSTYPE*, glslang::TParseContext&);
struct InputStackState {
    int kind; // 0 for lex. 1 for struct 2 for arr 3 scalar
    const glslang::TType* ttype;
    const YYSTYPE* stype;
    int tok;
    int reduce_n_ = 0;
};

static bool reduce_field_(Doc& doc, std::stack<InputStackState>& input_stack)
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

static bool reduce_subscript_(Doc& doc, std::stack<InputStackState>& input_stack)
{
    while (!input_stack.empty() && input_stack.top().tok != LEFT_BRACKET) {
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
    auto sizes = ttype->getArraySizes();
    if (top.reduce_n_ >= sizes->getNumDims()) {
        return false;
    }

    top.reduce_n_ += 1;

    if (top.reduce_n_ == sizes->getNumDims()) {
        if (top.ttype->isStruct()) {
            top.kind = 1;
        } else {
            top.kind = 3;
        }
    }

#if 0
    if (node->isArray()) {
        top.kind = 2;
    } else if (node->isStruct()) {
        top.kind = 1;
    } else {
        top.kind = 3;
    }

    top.ttype = &node->getType();
#endif
    return true;
}

static bool reduce_arr_(Doc& doc, const int line, const int col, std::stack<InputStackState>& input_stack)
{
    auto& top = input_stack.top();
    if (top.kind == 2) {
        return true;
    }

    if (top.kind != 0 || top.tok != IDENTIFIER) {
        return false;
    }

    auto* func = doc.lookup_func_by_line(line);
    auto* sym = doc.lookup_symbol_by_name(func, top.stype->lex.string->c_str());
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

static bool reduce_struct_(Doc& doc, const int line, const int col, std::stack<InputStackState>& input_stack)
{
    auto& top = input_stack.top();

    // not lex tok
    if (top.kind != 0 && top.kind != 1) {
        return false;
    }

    if (top.kind == 1)
        return true;

    auto tok = top.tok;
    if (tok != IDENTIFIER) {
        return false;
    }

    const glslang::TType* type = nullptr;
    auto* func = doc.lookup_func_by_line(line);
    auto* sym = doc.lookup_symbol_by_name(func, top.stype->lex.string->c_str());
    auto builtin = doc.lookup_builtin_symbols_by_prefix(top.stype->lex.string->c_str(), true);

    if (sym) {
        type = &sym->getType();
    } else if (!builtin.empty()) {
        auto* sym = builtin.front();
        if (auto* var = sym->getAsVariable()) {
            type = &var->getType();
        }
    } else {
        auto anons = doc.lookup_symbols_by_prefix(nullptr, "anon@");
        for (auto anon : anons) {
            if (type)
                break;

            if (!anon->isStruct()) {
                continue;
            }

            auto& members = *anon->getType().getStruct();
            for (int i = 0; i < members.size(); ++i) {
                auto member = members[i].type;
                std::string field = member->getFieldName().c_str();
                if (field == top.stype->lex.string->c_str()) {
                    type = member;
                    break;
                }
            }
        }
    }

    if (!type)
        return false;

    if (type->isReference()) {
        type = type->getReferentType();
    }

    if (!type->isStruct() && !type->isVector()) {
        return false;
    }

    top.kind = 1;
    top.ttype = type;
    return true;
}

static bool match_prefix(std::string const& s, std::string const& prefix)
{
    if (prefix.empty())
        return true;

    return prefix == s.substr(0, prefix.size());
}

static void do_complete_type_prefix_(Doc& doc, std::string const& prefix, std::vector<CompletionResult>& results)
{
    auto const& userdef_types = doc.userdef_types();
    for (auto* sym : userdef_types) {
        auto loc = sym->getLoc();
        auto const& ty = sym->getType();
        auto const* tyname = ty.getTypeName().c_str();
        if (!match_prefix(tyname, prefix))
            continue;

        CompletionResult r = {tyname, CompletionItemKind::Struct, ty.getCompleteString(true, false, false).c_str(), "",
                              tyname, InsertTextFormat::PlainText};
        results.push_back(r);
    }
}

static void do_complete_builtin_prefix_(Doc& doc, std::string const& prefix, std::vector<CompletionResult>& results)
{
    auto builtins = doc.lookup_builtin_symbols_by_prefix(prefix);

    for (auto* sym : builtins) {
        if (auto* var = sym->getAsVariable()) {
            auto* label = var->getName().c_str();
            std::string detail = var->getType().getCompleteString(true).c_str();
            CompletionResult result = {label, CompletionItemKind::Variable, detail, "",
                                       label, InsertTextFormat::PlainText};
            results.push_back(result);
        } else if (const auto* func = sym->getAsFunction()) {
            std::string func_name = func->getName().c_str();
            std::string return_type;
            if (func->getType().isStruct()) {
                return_type = func->getType().getTypeName().c_str();
            } else {
                return_type = func->getType().getBasicTypeString().c_str();
            }

            std::string args_list;
            std::string args_list_snippet;

            for (int i = 0; i < func->getParamCount(); ++i) {
                const auto& arg = (*func)[i];
                char buf[128];
                auto const& arg_type = *arg.type;
                const char* arg_type_str;
                if (arg_type.isStruct()) {
                    arg_type_str = arg_type.getTypeName().c_str();
                } else {
                    arg_type_str = arg_type.getBasicTypeString().c_str();
                }

                snprintf(buf, sizeof(buf), "${%d:%s %s}", i + 1, arg_type_str, arg.name ? arg.name->c_str() : "");

                args_list_snippet += buf;
                args_list_snippet += ", ";
                args_list = args_list + arg_type_str + " " + (arg.name ? arg.name->c_str() : "") + ", ";
            }

            if (args_list_snippet.size() > 2) {
                args_list_snippet.pop_back();
                args_list_snippet.pop_back();
                args_list.pop_back();
                args_list.pop_back();
            }
            std::string detail = return_type + " " + func_name + "(" + args_list + ")";
            std::string insert_text = func_name + "(" + args_list_snippet + ")";

            CompletionResult r = {func_name,   CompletionItemKind::Function, detail, "",
                                  insert_text, InsertTextFormat::Snippet};
            results.push_back(r);
        }
    }
}

static void do_complete_var_prefix_(Doc& doc, const int line, const int col, std::string const& prefix,
                                    std::vector<CompletionResult>& results)
{
    auto* func = doc.lookup_func_by_line(line);
    auto symbols = doc.lookup_symbols_by_prefix(func, prefix);
    for (auto const& sym : symbols) {
        auto detail = sym->getType().getCompleteString(true, false, false);
        auto label = sym->getName().c_str();

        CompletionResult r = {label, CompletionItemKind::Variable, detail.c_str(), "", label};
        results.emplace_back(r);
    }

    auto match_prefix = [&prefix](std::string const& field) {
        if (prefix.empty())
            return true;

        return prefix == field.substr(0, prefix.size());
    };

    auto& func_defs = doc.func_defs();
    auto norm_func_name = [](std::string const& fname) {
        auto pos = fname.find("(");
        if (pos == std::string::npos) {
            return fname;
        }

        return std::string(fname.begin(), fname.begin() + pos);
    };

    for (const auto& func : func_defs) {
        if (!match_prefix(func.def->getName().c_str())) {
            continue;
        }

        auto label = norm_func_name(func.def->getName().c_str());
        std::string return_type;
        auto const& rtype = func.def->getType();
        if (rtype.isStruct()) {
            return_type = rtype.getTypeName();
        } else {
            return_type = rtype.getBasicTypeString();
        }

        std::string args_list_snippet;
        std::string args_list;
        for (int i = 0; i < func.args.size(); ++i) {
            auto* arg = func.args[i];
            char buf[128];
            auto const& arg_type = arg->getType();
            const char* arg_type_str;
            if (arg_type.isStruct()) {
                arg_type_str = arg_type.getTypeName().c_str();
            } else {
                arg_type_str = arg_type.getBasicTypeString().c_str();
            }

            snprintf(buf, sizeof(buf), "${%d:%s %s}", i + 1, arg_type_str, arg->getName().c_str());

            args_list_snippet += buf;
            args_list_snippet += ", ";
            args_list = args_list + arg_type_str + " " + arg->getName().c_str() + ", ";
        }

        if (args_list_snippet.size() > 2) {
            args_list_snippet.pop_back();
            args_list_snippet.pop_back();
            args_list.pop_back();
            args_list.pop_back();
        }
        std::string detail = return_type + " " + label + "(" + args_list + ")";
        std::string insert_text = label + "(" + args_list_snippet + ")";

        CompletionResult r = {label, CompletionItemKind::Function, detail, "", insert_text, InsertTextFormat::Snippet};
        results.push_back(r);
    }
}

static void do_complete_struct_field_(const glslang::TType* ttype, std::string const& prefix,
                                      std::vector<CompletionResult>& results)
{
    auto match_prefix = [&prefix](std::string const& field) {
        if (prefix.empty())
            return true;

        return prefix == field.substr(0, prefix.size());
    };

    auto& members = *ttype->getStruct();
    for (int i = 0; i < members.size(); ++i) {
        auto field = members[i].type;
        auto label = field->getFieldName().c_str();
        auto kind = CompletionItemKind::Field;
        auto detail = field->getCompleteString(true, false, false);
        auto doc = "";
        if (match_prefix(label)) {
            results.push_back({label, kind, detail.c_str(), doc, label, InsertTextFormat::PlainText});
        }
    }
}

static void do_complete_exp_(Doc& doc, const int line, const int col, std::stack<InputStackState>& input_stack,
                             std::vector<CompletionResult>& results)
{
    if (input_stack.top().kind != 0) {
        return;
    }

    auto input = input_stack.top();
    input_stack.pop();

    if (input.tok == IDENTIFIER) {
        std::string prefix = input.stype->lex.string->c_str();
        if (input_stack.empty()) {
            do_complete_var_prefix_(doc, line, col, prefix, results);
            auto anons = doc.lookup_symbols_by_prefix(nullptr, "anon@");
            for (auto* anon : anons) {
                if (anon->isStruct()) {
                    do_complete_struct_field_(&anon->getType(), prefix, results);
                }
            }
            do_complete_type_prefix_(doc, prefix, results);
            do_complete_builtin_prefix_(doc, prefix, results);
            return;
        }

        auto top = input_stack.top();
        input_stack.pop();
        if (top.kind != 0) {
            auto* func = doc.lookup_func_by_line(line);
            auto symbols = doc.lookup_symbols_by_prefix(func, input.stype->lex.string->c_str());
            for (auto* sym : symbols) {
                std::string label = sym->getName().c_str();
                std::string detail = sym->getType().getCompleteString(true, false, false).c_str();
                CompletionResult r = {label, CompletionItemKind::Variable, detail, "", label};
                results.emplace_back(r);
            }
        } else if (top.tok == DOT) {
            if (input_stack.empty())
                return;
            auto& [kind, ttype, stype, tok, _] = input_stack.top();
            if (kind != 1) {
                return;
            }

            do_complete_struct_field_(ttype, prefix, results);
        }
    } else if (input.tok == DOT) {
        if (input_stack.empty())
            return;

        auto& [kind, ttype, stype, tok, _] = input_stack.top();
        if (kind != 1) {
            return;
        }

        if (ttype->isVector()) {
            std::string tyname = ttype->getBasicTypeString().c_str();
            const char* fields[] = {"x", "y", "z", "w"};
            for (auto i = 0; i < ttype->getVectorSize(); ++i) {
                results.push_back({fields[i], CompletionItemKind::Field, tyname + " " + fields[i], "", fields[i],
                                   InsertTextFormat::PlainText});
            }
        } else {
            auto& members = *ttype->getStruct();
            for (int i = 0; i < members.size(); ++i) {
                auto field = members[i].type;
                auto label = field->getFieldName().c_str();
                auto kind = CompletionItemKind::Field;
                auto detail = field->getCompleteString(true, false, false);
                auto doc = "";
                results.push_back({label, kind, detail.c_str(), doc, label, InsertTextFormat::PlainText});
            }
        }
    }
}

static std::vector<CompletionResult> do_complete(Doc& doc, std::vector<std::tuple<YYSTYPE, int>> const& lex_info,
                                                 const int line, const int col)
{
    //very tiny varaible exp parser
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
                do_complete_exp_(doc, line, col, input_stack, results);
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
                if (!reduce_struct_(doc, line, col, input_stack)) {
                    return results;
                }

                input_stack.push({0, nullptr, &stype, tok});
                state = EXPECT_IDENTIFIER;
                break;
            } else if (tok == LEFT_BRACKET) {
                if (!reduce_arr_(doc, line, col, input_stack)) {
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

std::vector<CompletionResult> completion(Doc& doc, std::string const& input, const int line, const int col)
{
    const char* source = input.data();
    size_t len = input.size();
    glslang::TInputScanner userInput(1, &source, &len);

    auto version = doc.intermediate()->getVersion();
    auto profile = doc.intermediate()->getProfile();
    auto stage = doc.intermediate()->getStage();
    auto spvVersion = doc.intermediate()->getSpv();
    auto entrypoint = doc.intermediate()->getEntryPointName();

    auto parser_resource = create_parser(version, profile, stage, spvVersion, entrypoint.c_str());
    parser_resource->ppcontext->setInput(userInput, false);
    parser_resource->parse_context->setScanner(&userInput);

    std::vector<std::tuple<YYSTYPE, int>> input_toks;
    while (true) {
        YYSTYPE stype;
        int tok = yylex(&stype, *parser_resource->parse_context);
        if (!tok) {
            break;
        }

        input_toks.push_back(std::make_tuple(stype, tok));
    }

    if (input_toks.empty())
        return {};

    input_toks.push_back({YYSTYPE{}, -1});
    return do_complete(doc, input_toks, line, col);
}
