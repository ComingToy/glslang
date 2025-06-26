#include "protocol.hpp"
#include "completion.hpp"
#include <cstdio>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

int Protocol::handle(nlohmann::json& req)
{
    nlohmann::json resp;
    std::cerr << "start handle protocol req: \n" << req.dump(4) << std::endl;

    std::string method = req["method"];
    if (method != "initialize" && !init_) {
        std::cerr << "received request but server is uninitialized." << std::endl;
        return 0;
    }

    if (method == "initialize") {
        initialize_(req);
    } else if (method == "initialized") {
        return 0;
    } else if (method == "workspace/didChangeConfiguration") {
        return 0;
    } else if (method == "textDocument/didOpen") {
        did_open_(req);
    } else if (method == "textDocument/definition") {
        definition_(req);
    } else if (method == "textDocument/didChange") {
        did_change_(req);
    } else if (method == "textDocument/completion") {
        completion_(req);
    } else if (method == "textDocument/didSave") {
        did_save_(req);
    }

    return 0;
}

void Protocol::make_response_(nlohmann::json& req, nlohmann::json* result)
{
    nlohmann::json body;
    if (result) {
        body = {
            {"jsonrpc", "2.0"},
            {"id", req["id"]},
            {"result", *result},
        };
    } else {
        body = nlohmann::json::parse(R"(
			{
				"jsonrpc": "2.0",
				"result": null
			}
		)");
        body["id"] = req["id"];
    }

    send_to_client_(body);
}

void Protocol::initialize_(nlohmann::json& req)
{
    auto result = nlohmann::json::parse(R"(
	{
		"capabilities": {
			"textDocumentSync": {
				"openClose": true,
				"change": 1,
				"save": true,
				"willSave": false 
			},
			"completionProvider": {
				"triggerCharacters": ["."],
				"resolveProvider": false,
				"completionItem": {
					"labelDetailsSupport": true
				}
			},
			"hoverProvider": false,
			"signatureHelpProvider": {
				"triggerCharacters": []
			},
			"declarationProvider": false,
			"definitionProvider": true,
			"typeDefinitionProvider": false,
			"implementationProvider": false,
			"referencesProvider": true,
			"documentHighlightProvider": false,
			"documentSymbolProvider": false,
			"codeActionProvider": false,
			"codeLensProvider": false,
			"documentLinkProvider": false,
			"colorProvider": false,
			"documentFormattingProvider": false,
			"documentRangeFormattingProvider": false,
			"documentOnTypeFormattingProvider": false,
			"renameProvider": false,
			"foldingRangeProvider": false,
			"executeCommandProvider": false,
			"selectionRangeProvider": false,
			"linkedEditingRangeProvider": false,
			"callHierarchyProvider": false,
			"semanticTokensProvider": false,
			"monikerProvider": false,
			"typeHierarchyProvider": false,
			"inlineValueProvider": false,
			"inlayHintProvider": false,
			"workspaceSymbolProvider": false
		}
	}
	)");

    // std::cerr << "build reqsp capabilities: " << std::endl << result.dump() << std::endl;
    nlohmann::json params = req["params"];
    workspace_.set_root(params["rootPath"]);

    init_ = true;
    // std::cerr << "init workspace at root: " << workspace_.get_root() << std::endl;
    make_response_(req, &result);
}

void Protocol::did_open_(nlohmann::json& req)
{
    if (!init_) {
        fprintf(stderr, "server is uninitialized\n");
        return;
    }

    auto& params = req["params"];
    auto& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"];
    int version = textDoc["version"];
    std::string source = textDoc["text"];
    Doc doc(uri, version, source);
    if (doc.parse({workspace_.get_root()})) {
        workspace_.add_doc(std::move(doc));
        publish_clear_diagnostics(uri);
    } else {
        publish_diagnostics(doc.info_log());
        fprintf(stderr, "open file %s failed.\n", uri.c_str());
    }
}

void Protocol::did_save_(nlohmann::json& req)
{
    auto& params = req["params"];
    std::string uri = params["textDocument"]["uri"];
    int version = params["textDocument"]["version"];

    auto [ret, doc] = workspace_.save_doc(uri, version);
    if (doc) {
        if (ret)
            publish_clear_diagnostics(uri);
        else
            publish_diagnostics(doc->info_log());
    }
}

static std::vector<std::string> split(const std::string& s, char delim)
{
    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

nlohmann::json Protocol::complete_field_(std::string const& uri, std::string const& input)
{
    std::cerr << "completion field with input: " << input << std::endl;
    nlohmann::json completion_items;

    auto terms = split(input, '.');
    if (input.back() == '.') {
        terms.push_back("");
    }

    auto* sym = workspace_.lookup_symbol_by_name(uri, terms[0]);
    if (!sym) {
        return completion_items;
    }

    std::cerr << "struct variable name: " << terms[0] << std::endl;
    if (terms.empty())
        return completion_items;

    std::vector<std::string> selectors;
    for (auto i = 1; i < (int)terms.size() - 1; ++i) {
        selectors.push_back(terms[i]);
        std::cerr << "selector " << terms[i] << std::endl;
    }

    std::string prefix = "";
    if (terms.size() >= 2) {
        prefix = terms.back();
    }

    std::cerr << "prefix: " << prefix << std::endl;

    auto match_prefix = [&prefix](std::string const& field) {
        if (prefix.empty())
            return true;

        return prefix == field.substr(0, prefix.size());
    };

    auto isstruct =
        sym->getType().isReference() ? sym->getType().getReferentType()->isStruct() : sym->getType().isStruct();
    if (!isstruct)
        return completion_items;

    const auto* members =
        sym->getType().isReference() ? sym->getType().getReferentType()->getStruct() : sym->getType().getStruct();

    if (!selectors.empty()) {
        for (auto& selector : selectors) {
            for (int i = 0; i < members->size(); ++i) {
                auto* member = (*members)[i].type;
                if (selector != member->getFieldName().c_str()) {
                    continue;
                }

                if (!member->isStruct()) {
                    return completion_items;
                }

                members = member->isReference() ? member->getReferentType()->getStruct() : member->getStruct();
                break;
            }
        }
    }

    for (int i = 0; i < members->size(); ++i) {
        const auto field = (*members)[i].type;
        auto label = field->getFieldName();
        if (!match_prefix(label.c_str())) {
            continue;
        }
        auto ftypename = field->isStruct() ? field->getCompleteString(true, false, false)
                                           : field->getCompleteString(true, false, false);

        int kind = 5; // field
        nlohmann::json item;
        item["label"] = label;
        item["kind"] = kind;
        item["detail"] = ftypename;

        std::string documentation = "";
        item["documentation"] = documentation;
        completion_items.push_back(item);
    }

    return completion_items;
}

nlohmann::json Protocol::complete_variable_(std::string const& uri, std::string const& input)
{
    auto symbols = workspace_.lookup_symbols_by_prefix(uri, input);

    nlohmann::json completion_items;
    for (auto sym : symbols) {
        std::string label = sym->getName().c_str();
        const auto& type = sym->getType();
        auto typname =
            type.isStruct() ? type.getCompleteString(true, false, false) : type.getCompleteString(true, false, false);

        int kind = 6; //variable
        std::string detail = typname.c_str();
        std::string documentation = "";

        nlohmann::json item;
        item["label"] = label;
        item["kind"] = kind;
        item["detail"] = detail;
        item["documentation"] = documentation;

        completion_items.push_back(item);
    }

    return completion_items;
}

void Protocol::completion_(nlohmann::json& req)
{
    auto& params = req["params"];
    // int triggerKind = params["context"]["triggerKind"];
    int line = params["position"]["line"];
    int col = params["position"]["character"];
    std::string uri = params["textDocument"]["uri"];

    auto term = workspace_.get_term(uri, line, col);

    auto complete_results = completion(*workspace_.get_doc(uri), term);

    nlohmann::json completion_items;
    for (auto const& result : complete_results) {
        nlohmann::json item;
        item["label"] = result.label;
        item["kind"] = int(result.kind);
        item["detail"] = result.detail;
        item["documentation"] = result.documentation;
        completion_items.push_back(item);
    }

    make_response_(req, &completion_items);
}

void Protocol::definition_(nlohmann::json& req)
{
    if (!init_) {
        fprintf(stderr, "server is uninitialized\n");
        return;
    }
    std::cerr << "handle goto definition" << std::endl;

    auto& params = req["params"];
    std::string uri = params["textDocument"]["uri"];
    int col = params["position"]["character"];
    int line = params["position"]["line"];

    std::cerr << "target sym at " << line << ":" << col << std::endl;
    auto loc = workspace_.locate_symbol_def(uri, line + 1, col + 1);

    if (loc.name) {
        nlohmann::json result;
        nlohmann::json start = {{"line", loc.line - 1}, {"character", loc.column - 1}};
        result["uri"] = loc.name->c_str();
        result["range"] = {{"start", start}, {"end", start}};
        make_response_(req, &result);
    } else {
        make_response_(req, nullptr);
    }
}

void Protocol::did_change_(nlohmann::json& req)
{
    auto& params = req["params"];
    auto& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"];
    int version = textDoc["version"];
    std::string source = params["contentChanges"][0]["text"];
    workspace_.update_doc(uri, version, source);
}

void Protocol::publish_(std::string const& method, nlohmann::json* params)
{
    nlohmann::json body;
    if (!params) {
        body = nlohmann::json::parse(R"(
			{
				"params": {}
			}
		)");
        body["method"] = method;
    } else {
        body = {{"method", method}, {"params", *params}};
    }

    send_to_client_(body);
}

void Protocol::publish_diagnostics(std::string const& error)
{
    std::stringstream ss(error);
    std::string line;
    std::map<std::string, nlohmann::json> diagnostics;

    while (std::getline(ss, line)) {
        std::smatch result;
        std::regex pattern("ERROR: (file:///.*):([0-9]+): (.*)");
        if (std::regex_match(line, result, pattern)) {
            std::string uri = result[1].str();
            int row = std::atoi(result[2].str().c_str()) - 1;
            std::string message = result[3].str();
            nlohmann::json start = {{"line", row}, {"character", 1}};
            nlohmann::json diagnostic = {{"range", {{"start", start}, {"end", start}}}, {"message", message}};
            diagnostics[uri].push_back(diagnostic);
        }
    }

    for (auto& [uri, diagnostic] : diagnostics) {
        nlohmann::json body = {{"uri", uri}, {"diagnostics", diagnostic}};
        publish_("textDocument/publishDiagnostics", &body);
    }
}

void Protocol::publish_clear_diagnostics(const std::string& uri)
{
    nlohmann::json body = nlohmann::json::parse(R"(
	{
		"diagnostics": []
	}
	)");
    body["uri"] = uri;
    publish_("textDocument/publishDiagnostics", &body);
}

void Protocol::send_to_client_(nlohmann::json& content)
{
    std::string body_str = content.dump();

    std::string header;
    header.append("Content-Length: ");
    header.append(std::to_string(body_str.size()) + "\r\n");
    header.append("Content-Type: application/vscode-jsonrpc;charset=utf-8\r\n");
    header.append("\r\n");
    header.append(body_str);
    std::cerr << "resp to client: \n" << header << std::endl;
    std::cout << header;
    std::flush(std::cout);
}
