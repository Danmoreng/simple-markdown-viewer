#include "render/syntax/tree_sitter_highlighter.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string_view>

#include "render/syntax/tree_sitter_queries.h"
#include "tree_sitter/api.h"

extern "C" {
const TSLanguage* tree_sitter_c();
const TSLanguage* tree_sitter_cpp();
const TSLanguage* tree_sitter_javascript();
const TSLanguage* tree_sitter_typescript();
const TSLanguage* tree_sitter_tsx();
const TSLanguage* tree_sitter_json();
const TSLanguage* tree_sitter_python();
const TSLanguage* tree_sitter_bash();
}

namespace mdviewer::syntax {
namespace {

struct ParserDeleter {
    void operator()(TSParser* parser) const {
        ts_parser_delete(parser);
    }
};

struct TreeDeleter {
    void operator()(TSTree* tree) const {
        ts_tree_delete(tree);
    }
};

struct QueryDeleter {
    void operator()(TSQuery* query) const {
        ts_query_delete(query);
    }
};

struct QueryCursorDeleter {
    void operator()(TSQueryCursor* cursor) const {
        ts_query_cursor_delete(cursor);
    }
};

struct LanguageDefinition {
    const TSLanguage* language = nullptr;
    const char* query = nullptr;
};

struct HighlightRange {
    size_t start = 0;
    size_t end = 0;
    InlineStyle style = InlineStyle::Plain;
    int priority = 0;
};

std::string NormalizeLanguage(std::string_view language) {
    std::string normalized;
    normalized.reserve(language.size());
    for (char ch : language) {
        if (ch == '_' || ch == '-' || ch == ' ') {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

LanguageDefinition ResolveLanguage(const std::string& language) {
    const std::string normalized = NormalizeLanguage(language);
    if (normalized == "c" || normalized == "h") {
        return {tree_sitter_c(), GetTreeSitterCQuery()};
    }
    if (normalized == "cpp" || normalized == "c++" || normalized == "cc" ||
        normalized == "cxx" || normalized == "hpp" || normalized == "hh" || normalized == "hxx") {
        return {tree_sitter_cpp(), GetTreeSitterCppQuery()};
    }
    if (normalized == "javascript" || normalized == "js" || normalized == "jsx" ||
        normalized == "mjs" || normalized == "cjs") {
        return {tree_sitter_javascript(), GetTreeSitterJavaScriptQuery()};
    }
    if (normalized == "typescript" || normalized == "ts") {
        return {tree_sitter_typescript(), GetTreeSitterTypeScriptQuery()};
    }
    if (normalized == "tsx") {
        return {tree_sitter_tsx(), GetTreeSitterTsxQuery()};
    }
    if (normalized == "json" || normalized == "jsonc") {
        return {tree_sitter_json(), GetTreeSitterJsonQuery()};
    }
    if (normalized == "python" || normalized == "py") {
        return {tree_sitter_python(), GetTreeSitterPythonQuery()};
    }
    if (normalized == "bash" || normalized == "sh" || normalized == "shell" || normalized == "zsh") {
        return {tree_sitter_bash(), GetTreeSitterBashQuery()};
    }
    return {};
}

InlineStyle StyleForCapture(std::string_view capture) {
    if (capture == "comment") {
        return InlineStyle::SyntaxComment;
    }
    if (capture == "string" || capture == "string.special" || capture == "character" ||
        capture == "escape" || capture == "embedded") {
        return InlineStyle::SyntaxString;
    }
    if (capture == "number" || capture == "float") {
        return InlineStyle::SyntaxNumber;
    }
    if (capture == "keyword" || capture == "keyword.conditional" || capture == "keyword.coroutine" ||
        capture == "keyword.directive" || capture == "keyword.exception" || capture == "keyword.function" ||
        capture == "keyword.import" || capture == "keyword.operator" || capture == "keyword.repeat" ||
        capture == "keyword.return" || capture == "keyword.storage" || capture == "keyword.type") {
        return InlineStyle::SyntaxKeyword;
    }
    if (capture == "operator") {
        return InlineStyle::SyntaxOperator;
    }
    if (capture == "punctuation.bracket" || capture == "punctuation.delimiter" ||
        capture == "punctuation.special") {
        return InlineStyle::SyntaxPunctuation;
    }
    if (capture == "function" || capture == "function.builtin" || capture == "function.call" ||
        capture == "function.macro" || capture == "method" || capture == "method.call" ||
        capture == "constructor") {
        return InlineStyle::SyntaxFunction;
    }
    if (capture == "type" || capture == "type.builtin" || capture == "type.definition" ||
        capture == "module" || capture == "namespace" || capture == "tag") {
        return InlineStyle::SyntaxType;
    }
    if (capture == "constant" || capture == "constant.builtin" || capture == "boolean" ||
        capture == "null" || capture == "attribute") {
        return InlineStyle::SyntaxConstant;
    }
    if (capture == "property" || capture == "variable" || capture == "variable.builtin" ||
        capture == "variable.member" || capture == "variable.parameter" || capture == "parameter") {
        return InlineStyle::SyntaxVariable;
    }
    return InlineStyle::Plain;
}

int PriorityForStyle(InlineStyle style) {
    switch (style) {
        case InlineStyle::SyntaxComment: return 90;
        case InlineStyle::SyntaxString: return 80;
        case InlineStyle::SyntaxKeyword: return 70;
        case InlineStyle::SyntaxFunction: return 60;
        case InlineStyle::SyntaxType: return 55;
        case InlineStyle::SyntaxNumber: return 50;
        case InlineStyle::SyntaxConstant: return 45;
        case InlineStyle::SyntaxVariable: return 35;
        case InlineStyle::SyntaxOperator: return 30;
        case InlineStyle::SyntaxPunctuation: return 20;
        default: return 0;
    }
}

std::string MergeCodeText(const std::vector<InlineRun>& runs) {
    std::string text;
    for (const auto& run : runs) {
        text += run.text;
    }
    return text;
}

std::vector<HighlightRange> CollectHighlightRanges(
    const std::string& text,
    const LanguageDefinition& definition) {
    std::vector<HighlightRange> ranges;
    if (text.empty() || definition.language == nullptr ||
        definition.query == nullptr || std::strlen(definition.query) == 0) {
        return ranges;
    }

    std::unique_ptr<TSParser, ParserDeleter> parser(ts_parser_new());
    if (!parser || !ts_parser_set_language(parser.get(), definition.language)) {
        return ranges;
    }

    std::unique_ptr<TSTree, TreeDeleter> tree(
        ts_parser_parse_string(parser.get(), nullptr, text.c_str(), static_cast<uint32_t>(text.size())));
    if (!tree) {
        return ranges;
    }

    uint32_t errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;
    std::unique_ptr<TSQuery, QueryDeleter> query(ts_query_new(
        definition.language,
        definition.query,
        static_cast<uint32_t>(std::strlen(definition.query)),
        &errorOffset,
        &errorType));
    if (!query) {
        return ranges;
    }

    std::unique_ptr<TSQueryCursor, QueryCursorDeleter> cursor(ts_query_cursor_new());
    if (!cursor) {
        return ranges;
    }

    ts_query_cursor_exec(cursor.get(), query.get(), ts_tree_root_node(tree.get()));

    TSQueryMatch match;
    uint32_t captureIndex = 0;
    while (ts_query_cursor_next_capture(cursor.get(), &match, &captureIndex)) {
        const TSQueryCapture& capture = match.captures[captureIndex];
        uint32_t captureNameLength = 0;
        const char* captureName = ts_query_capture_name_for_id(
            query.get(),
            capture.index,
            &captureNameLength);
        const InlineStyle style = StyleForCapture(std::string_view(captureName, captureNameLength));
        if (style == InlineStyle::Plain) {
            continue;
        }

        const size_t start = ts_node_start_byte(capture.node);
        const size_t end = ts_node_end_byte(capture.node);
        if (start >= end || end > text.size()) {
            continue;
        }

        ranges.push_back({start, end, style, PriorityForStyle(style)});
    }

    std::sort(ranges.begin(), ranges.end(), [](const HighlightRange& left, const HighlightRange& right) {
        if (left.start != right.start) return left.start < right.start;
        if (left.priority != right.priority) return left.priority > right.priority;
        return (left.end - left.start) < (right.end - right.start);
    });

    std::vector<HighlightRange> accepted;
    for (const auto& range : ranges) {
        bool overlaps = false;
        for (const auto& existing : accepted) {
            if (range.start < existing.end && range.end > existing.start) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) {
            accepted.push_back(range);
        }
    }

    std::sort(accepted.begin(), accepted.end(), [](const HighlightRange& left, const HighlightRange& right) {
        return left.start < right.start;
    });
    return accepted;
}

void AppendRun(std::vector<InlineRun>& runs, InlineStyle style, std::string_view text) {
    if (text.empty()) {
        return;
    }
    if (!runs.empty() && runs.back().style == style && runs.back().url.empty()) {
        runs.back().text.append(text);
        return;
    }
    runs.push_back({style, std::string(text), ""});
}

} // namespace

std::vector<InlineRun> HighlightCodeBlock(const std::string& language, const std::vector<InlineRun>& runs) {
    const std::string text = MergeCodeText(runs);
    const LanguageDefinition definition = ResolveLanguage(language);
    std::vector<HighlightRange> ranges = CollectHighlightRanges(text, definition);
    if (ranges.empty()) {
        return runs;
    }

    std::vector<InlineRun> highlightedRuns;
    size_t cursor = 0;
    for (const auto& range : ranges) {
        if (cursor < range.start) {
            AppendRun(highlightedRuns, InlineStyle::Plain, std::string_view(text.data() + cursor, range.start - cursor));
        }
        AppendRun(highlightedRuns, range.style, std::string_view(text.data() + range.start, range.end - range.start));
        cursor = range.end;
    }
    if (cursor < text.size()) {
        AppendRun(highlightedRuns, InlineStyle::Plain, std::string_view(text.data() + cursor, text.size() - cursor));
    }
    return highlightedRuns;
}

} // namespace mdviewer::syntax
