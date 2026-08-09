#include "render/syntax/tree_sitter_highlighter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>

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
const TSLanguage* tree_sitter_rust();
const TSLanguage* tree_sitter_go();
const TSLanguage* tree_sitter_c_sharp();
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
    const char* cacheKey = nullptr;
};

struct HighlightRange {
    size_t start = 0;
    size_t end = 0;
    SyntaxRole role = SyntaxRole::None;
    int priority = 0;
};

enum class CollectionStatus {
    Succeeded,
    TimedOut,
    Failed,
};

struct RangeCollection {
    std::vector<HighlightRange> ranges;
    CollectionStatus status = CollectionStatus::Succeeded;
};

struct Deadline {
    std::chrono::steady_clock::time_point expiresAt;
    bool timedOut = false;

    bool HasExpired() {
        if (std::chrono::steady_clock::now() < expiresAt) {
            return false;
        }
        timedOut = true;
        return true;
    }
};

struct TextInput {
    const std::string* text = nullptr;
};

struct CacheKey {
    std::string language;
    std::string source;

    bool operator==(const CacheKey& other) const {
        return language == other.language && source == other.source;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const {
        const size_t languageHash = std::hash<std::string>{}(key.language);
        const size_t sourceHash = std::hash<std::string>{}(key.source);
        return languageHash ^ (sourceHash + 0x9e3779b9U + (languageHash << 6U) + (languageHash >> 2U));
    }
};

struct CacheEntry {
    CacheKey key;
    HighlightResult result;
    size_t sizeBytes = 0;
};

constexpr size_t kMaxCacheEntries = 128;
constexpr size_t kMaxCacheBytes = 16U * 1024U * 1024U;
// These limits only bound retained cache memory; they never prevent a block
// from being highlighted.

using CacheEntries = std::list<CacheEntry>;

struct HighlightCache {
    std::mutex mutex;
    CacheEntries entries;
    std::unordered_map<CacheKey, CacheEntries::iterator, CacheKeyHash> index;
    size_t sizeBytes = 0;
    size_t hits = 0;
    size_t misses = 0;
};

HighlightCache& Cache() {
    static HighlightCache cache;
    return cache;
}

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
        return {tree_sitter_c(), GetTreeSitterCQuery(), "c"};
    }
    if (normalized == "cpp" || normalized == "c++" || normalized == "cc" ||
        normalized == "cxx" || normalized == "hpp" || normalized == "hh" || normalized == "hxx") {
        return {tree_sitter_cpp(), GetTreeSitterCppQuery(), "cpp"};
    }
    if (normalized == "javascript" || normalized == "js" || normalized == "jsx" ||
        normalized == "mjs" || normalized == "cjs") {
        return {tree_sitter_javascript(), GetTreeSitterJavaScriptQuery(), "javascript"};
    }
    if (normalized == "typescript" || normalized == "ts") {
        return {tree_sitter_typescript(), GetTreeSitterTypeScriptQuery(), "typescript"};
    }
    if (normalized == "tsx") {
        return {tree_sitter_tsx(), GetTreeSitterTsxQuery(), "tsx"};
    }
    if (normalized == "json" || normalized == "jsonc") {
        return {tree_sitter_json(), GetTreeSitterJsonQuery(), "json"};
    }
    if (normalized == "python" || normalized == "py") {
        return {tree_sitter_python(), GetTreeSitterPythonQuery(), "python"};
    }
    if (normalized == "bash" || normalized == "sh" || normalized == "shell" || normalized == "zsh") {
        return {tree_sitter_bash(), GetTreeSitterBashQuery(), "bash"};
    }
    if (normalized == "rust" || normalized == "rs") {
        return {tree_sitter_rust(), GetTreeSitterRustQuery(), "rust"};
    }
    if (normalized == "go" || normalized == "golang") {
        return {tree_sitter_go(), GetTreeSitterGoQuery(), "go"};
    }
    if (normalized == "csharp" || normalized == "c#" || normalized == "cs") {
        return {tree_sitter_c_sharp(), GetTreeSitterCSharpQuery(), "csharp"};
    }
    return {};
}

SyntaxRole RoleForCapture(std::string_view capture) {
    if (capture == "comment") {
        return SyntaxRole::Comment;
    }
    if (capture == "string" || capture == "string.special" || capture == "character" ||
        capture == "escape" || capture == "embedded") {
        return SyntaxRole::String;
    }
    if (capture == "number" || capture == "float") {
        return SyntaxRole::Number;
    }
    if (capture == "keyword" || capture == "keyword.conditional" || capture == "keyword.coroutine" ||
        capture == "keyword.directive" || capture == "keyword.exception" || capture == "keyword.function" ||
        capture == "keyword.import" || capture == "keyword.operator" || capture == "keyword.repeat" ||
        capture == "keyword.return" || capture == "keyword.storage" || capture == "keyword.type") {
        return SyntaxRole::Keyword;
    }
    if (capture == "operator") {
        return SyntaxRole::Operator;
    }
    if (capture == "punctuation.bracket" || capture == "punctuation.delimiter" ||
        capture == "punctuation.special") {
        return SyntaxRole::Punctuation;
    }
    if (capture == "function" || capture == "function.builtin" || capture == "function.call" ||
        capture == "function.macro" || capture == "method" || capture == "method.call" ||
        capture == "constructor") {
        return SyntaxRole::Function;
    }
    if (capture == "type" || capture == "type.builtin" || capture == "type.definition" ||
        capture == "module" || capture == "namespace" || capture == "tag") {
        return SyntaxRole::Type;
    }
    if (capture == "constant" || capture == "constant.builtin" || capture == "boolean" ||
        capture == "null" || capture == "attribute") {
        return SyntaxRole::Constant;
    }
    if (capture == "property" || capture == "variable" || capture == "variable.builtin" ||
        capture == "variable.member" || capture == "variable.parameter" || capture == "parameter") {
        return SyntaxRole::Variable;
    }
    return SyntaxRole::None;
}

int PriorityForRole(SyntaxRole role) {
    switch (role) {
        case SyntaxRole::Comment: return 90;
        case SyntaxRole::String: return 80;
        case SyntaxRole::Keyword: return 70;
        case SyntaxRole::Function: return 60;
        case SyntaxRole::Type: return 55;
        case SyntaxRole::Number: return 50;
        case SyntaxRole::Constant: return 45;
        case SyntaxRole::Variable: return 35;
        case SyntaxRole::Operator: return 30;
        case SyntaxRole::Punctuation: return 20;
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

const char* ReadTextInput(
    void* payload,
    uint32_t byteIndex,
    TSPoint,
    uint32_t* bytesRead) {
    const auto* input = static_cast<TextInput*>(payload);
    if (input == nullptr || input->text == nullptr || byteIndex >= input->text->size()) {
        *bytesRead = 0;
        return nullptr;
    }

    const size_t remaining = input->text->size() - byteIndex;
    *bytesRead = static_cast<uint32_t>(std::min(
        remaining,
        static_cast<size_t>(std::numeric_limits<uint32_t>::max())));
    return input->text->data() + byteIndex;
}

bool ParseProgressCallback(TSParseState* state) {
    auto* deadline = static_cast<Deadline*>(state->payload);
    return deadline != nullptr && deadline->HasExpired();
}

bool QueryProgressCallback(TSQueryCursorState* state) {
    auto* deadline = static_cast<Deadline*>(state->payload);
    return deadline != nullptr && deadline->HasExpired();
}

size_t EstimateCacheEntrySize(const CacheKey& key, const HighlightResult& result) {
    size_t size = (key.language.size() + key.source.size()) * 2U;
    for (const auto& run : result.runs) {
        size += run.text.size() + run.imageSource.size() + run.linkTarget.size() + sizeof(InlineRun);
    }
    return size;
}

bool TryGetCachedResult(const CacheKey& key, HighlightResult& result) {
    HighlightCache& cache = Cache();
    std::lock_guard lock(cache.mutex);
    const auto found = cache.index.find(key);
    if (found == cache.index.end()) {
        ++cache.misses;
        return false;
    }

    ++cache.hits;
    cache.entries.splice(cache.entries.begin(), cache.entries, found->second);
    result = found->second->result;
    return true;
}

void StoreCachedResult(CacheKey key, const HighlightResult& result) {
    const size_t entrySize = EstimateCacheEntrySize(key, result);
    if (entrySize > kMaxCacheBytes) {
        return;
    }

    HighlightCache& cache = Cache();
    std::lock_guard lock(cache.mutex);
    const auto existing = cache.index.find(key);
    if (existing != cache.index.end()) {
        cache.sizeBytes -= existing->second->sizeBytes;
        cache.entries.erase(existing->second);
        cache.index.erase(existing);
    }

    cache.entries.push_front({std::move(key), result, entrySize});
    cache.index.emplace(cache.entries.front().key, cache.entries.begin());
    cache.sizeBytes += entrySize;

    while (cache.entries.size() > kMaxCacheEntries || cache.sizeBytes > kMaxCacheBytes) {
        const auto last = std::prev(cache.entries.end());
        cache.sizeBytes -= last->sizeBytes;
        cache.index.erase(last->key);
        cache.entries.erase(last);
    }
}

RangeCollection CollectHighlightRanges(
    const std::string& text,
    const LanguageDefinition& definition,
    Deadline& deadline) {
    RangeCollection collection;
    if (text.empty() || definition.language == nullptr ||
        definition.query == nullptr || std::strlen(definition.query) == 0) {
        return collection;
    }
    if (deadline.HasExpired()) {
        collection.status = CollectionStatus::TimedOut;
        return collection;
    }

    std::unique_ptr<TSParser, ParserDeleter> parser(ts_parser_new());
    if (!parser || !ts_parser_set_language(parser.get(), definition.language)) {
        collection.status = CollectionStatus::Failed;
        return collection;
    }

    TextInput textInput{&text};
    const TSInput input{&textInput, ReadTextInput, TSInputEncodingUTF8, nullptr};
    const TSParseOptions parseOptions{&deadline, ParseProgressCallback};
    std::unique_ptr<TSTree, TreeDeleter> tree(ts_parser_parse_with_options(
        parser.get(),
        nullptr,
        input,
        parseOptions));
    if (!tree) {
        collection.status = deadline.timedOut ? CollectionStatus::TimedOut : CollectionStatus::Failed;
        return collection;
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
        collection.status = CollectionStatus::Failed;
        return collection;
    }

    std::unique_ptr<TSQueryCursor, QueryCursorDeleter> cursor(ts_query_cursor_new());
    if (!cursor) {
        collection.status = CollectionStatus::Failed;
        return collection;
    }

    const TSQueryCursorOptions queryOptions{&deadline, QueryProgressCallback};
    ts_query_cursor_exec_with_options(
        cursor.get(),
        query.get(),
        ts_tree_root_node(tree.get()),
        &queryOptions);

    TSQueryMatch match;
    uint32_t captureIndex = 0;
    while (ts_query_cursor_next_capture(cursor.get(), &match, &captureIndex)) {
        if (deadline.HasExpired()) {
            collection.status = CollectionStatus::TimedOut;
            collection.ranges.clear();
            return collection;
        }
        const TSQueryCapture& capture = match.captures[captureIndex];
        uint32_t captureNameLength = 0;
        const char* captureName = ts_query_capture_name_for_id(
            query.get(),
            capture.index,
            &captureNameLength);
        const SyntaxRole role = RoleForCapture(std::string_view(captureName, captureNameLength));
        if (role == SyntaxRole::None) {
            continue;
        }

        const size_t start = ts_node_start_byte(capture.node);
        const size_t end = ts_node_end_byte(capture.node);
        if (start >= end || end > text.size()) {
            continue;
        }

        collection.ranges.push_back({start, end, role, PriorityForRole(role)});
    }
    if (deadline.timedOut) {
        collection.status = CollectionStatus::TimedOut;
        collection.ranges.clear();
        return collection;
    }

    std::sort(collection.ranges.begin(), collection.ranges.end(), [](const HighlightRange& left, const HighlightRange& right) {
        if (left.start != right.start) return left.start < right.start;
        if (left.priority != right.priority) return left.priority > right.priority;
        return (left.end - left.start) < (right.end - right.start);
    });

    std::vector<HighlightRange> accepted;
    accepted.reserve(collection.ranges.size());
    for (const auto& range : collection.ranges) {
        // Ranges arrive in start order and accepted ranges never overlap. Therefore
        // only the most recently accepted range can overlap the next candidate.
        if (accepted.empty() || range.start >= accepted.back().end) {
            accepted.push_back(range);
        }
    }

    collection.ranges = std::move(accepted);
    return collection;
}

void AppendRun(std::vector<InlineRun>& runs, SyntaxRole role, std::string_view text) {
    if (text.empty()) {
        return;
    }
    if (!runs.empty() &&
        runs.back().kind == InlineKind::Text &&
        runs.back().formatting == InlineFormatting::None &&
        runs.back().syntaxRole == role &&
        runs.back().imageSource.empty() &&
        runs.back().linkTarget.empty()) {
        runs.back().text.append(text);
        return;
    }
    runs.push_back(InlineRun{
        .syntaxRole = role,
        .text = std::string(text),
    });
}

} // namespace

HighlightResult HighlightCodeBlock(
    const std::string& language,
    const std::vector<InlineRun>& runs,
    const HighlightOptions& options) {
    HighlightResult result{runs, HighlightStatus::NotRequested};
    try {
        const LanguageDefinition definition = ResolveLanguage(language);
        if (definition.language == nullptr || definition.query == nullptr || definition.cacheKey == nullptr) {
            result.status = HighlightStatus::UnsupportedLanguage;
            return result;
        }

        std::string source = MergeCodeText(runs);
        CacheKey cacheKey{definition.cacheKey, std::move(source)};
        if (options.useCache && TryGetCachedResult(cacheKey, result)) {
            return result;
        }

        const std::string& text = cacheKey.source;
        Deadline deadline{std::chrono::steady_clock::now() + options.timeBudget};
        RangeCollection collection = CollectHighlightRanges(text, definition, deadline);
        if (collection.status == CollectionStatus::TimedOut) {
            result.status = HighlightStatus::TimedOut;
            return result;
        }
        if (collection.status == CollectionStatus::Failed) {
            result.status = HighlightStatus::Failed;
            return result;
        }
        if (collection.ranges.empty()) {
            result.status = HighlightStatus::NoHighlights;
            if (options.useCache) {
                StoreCachedResult(std::move(cacheKey), result);
            }
            return result;
        }

        std::vector<InlineRun> highlightedRuns;
        size_t cursor = 0;
        for (const auto& range : collection.ranges) {
            if (cursor < range.start) {
                AppendRun(
                    highlightedRuns,
                    SyntaxRole::None,
                    std::string_view(text.data() + cursor, range.start - cursor));
            }
            AppendRun(
                highlightedRuns,
                range.role,
                std::string_view(text.data() + range.start, range.end - range.start));
            cursor = range.end;
        }
        if (cursor < text.size()) {
            AppendRun(
                highlightedRuns,
                SyntaxRole::None,
                std::string_view(text.data() + cursor, text.size() - cursor));
        }

        result.runs = std::move(highlightedRuns);
        result.status = HighlightStatus::Highlighted;
        if (options.useCache) {
            StoreCachedResult(std::move(cacheKey), result);
        }
        return result;
    } catch (...) {
        result.runs = runs;
        result.status = HighlightStatus::Failed;
        return result;
    }
}

HighlightCacheStats GetHighlightCacheStats() {
    HighlightCache& cache = Cache();
    std::lock_guard lock(cache.mutex);
    return {cache.hits, cache.misses, cache.entries.size()};
}

void ClearHighlightCache() {
    HighlightCache& cache = Cache();
    std::lock_guard lock(cache.mutex);
    cache.entries.clear();
    cache.index.clear();
    cache.sizeBytes = 0;
    cache.hits = 0;
    cache.misses = 0;
}

} // namespace mdviewer::syntax
