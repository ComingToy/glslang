#include "protocol.hpp"
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
    if (method == "initialize") {
        // std::cerr << "start handle initialize req" << std::endl;
        initialize_(req);
    } else if (method == "initialized") {
        return 0;
    } else if (method == "workspace/didChangeConfiguration") {
        return 0;
    } else if (method == "textDocument/didOpen") {
        didOpen_(req);
    } else if (method == "textDocument/definition") {
        definition_(req);
    } else if (method == "textDocument/didChange") {
        didChange_(req);
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
				"change": 1
			},
			"completionProvider": {
				"triggerCharacters": [],
				"resolveProvider": true,
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

void Protocol::didOpen_(nlohmann::json& req)
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
        workspace_.update_doc(std::move(doc));
    } else {
        publish_diagnostics(doc.info_log());
        fprintf(stderr, "open file %s failed.\n", uri.c_str());
    }
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

void Protocol::didChange_(nlohmann::json& req)
{
    auto& params = req["params"];
    auto& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"];
    int version = textDoc["version"];
    std::string source = params["contentChanges"][0]["text"];

    Doc doc(uri, version, source);
    if (doc.parse({workspace_.get_root()})) {
        workspace_.update_doc(std::move(doc));
    } else {
        publish_diagnostics(doc.info_log());
        fprintf(stderr, "update doc %s failed.\n", uri.c_str());
    }
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
