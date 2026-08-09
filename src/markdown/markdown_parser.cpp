#include "markdown_parser.h"
#include "md4c.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <stack>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mdviewer {

namespace {
constexpr size_t kMaxMarkdownNestingDepth = 256;

struct FrontMatter {
    std::string format;
    std::string content;
    size_t bodyOffset = 0;
};

std::string_view TrimAscii(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::pair<std::string_view, size_t> ReadLine(std::string_view source, size_t position) {
    const size_t lineEnd = source.find('\n', position);
    const size_t contentEnd = lineEnd == std::string_view::npos ? source.size() : lineEnd;
    std::string_view line = source.substr(position, contentEnd - position);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return {line, lineEnd == std::string_view::npos ? source.size() : lineEnd + 1};
}

bool HasDelimitedMetadataKey(std::string_view content, char separator) {
    size_t position = 0;
    while (position < content.size()) {
        const auto [rawLine, next] = ReadLine(content, position);
        const std::string_view line = TrimAscii(rawLine);
        position = next;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const size_t separatorPosition = line.find(separator);
        if (separatorPosition == std::string_view::npos) {
            continue;
        }
        const std::string_view key = TrimAscii(line.substr(0, separatorPosition));
        if (key.empty() || (!std::isalpha(static_cast<unsigned char>(key.front())) && key.front() != '_')) {
            continue;
        }
        if (std::all_of(key.begin() + 1, key.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
            })) {
            return true;
        }
    }
    return false;
}

std::optional<FrontMatter> ExtractDelimitedFrontMatter(
    std::string_view source,
    size_t start,
    std::string_view delimiter,
    std::string format,
    char keySeparator) {
    const auto [firstLine, contentStart] = ReadLine(source, start);
    if (TrimAscii(firstLine) != delimiter || contentStart >= source.size()) {
        return std::nullopt;
    }

    size_t lineStart = contentStart;
    while (lineStart < source.size()) {
        const auto [line, next] = ReadLine(source, lineStart);
        const std::string_view trimmed = TrimAscii(line);
        const bool closesYaml = delimiter == "---" && trimmed == "...";
        if (trimmed == delimiter || closesYaml) {
            const std::string_view content = source.substr(contentStart, lineStart - contentStart);
            if (!HasDelimitedMetadataKey(content, keySeparator)) {
                return std::nullopt;
            }
            return FrontMatter{
                .format = std::move(format),
                .content = std::string(TrimAscii(content)),
                .bodyOffset = next,
            };
        }
        if (next <= lineStart) {
            break;
        }
        lineStart = next;
    }
    return std::nullopt;
}

bool IsTypicalJsonMetadataKey(std::string_view key) {
    static const std::unordered_set<std::string> keys = {
        "author", "authors", "categories", "date", "description", "draft", "keywords",
        "lang", "language", "layout", "slug", "summary", "tags", "title", "updated",
    };
    std::string normalized(key);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return keys.contains(normalized);
}

std::optional<FrontMatter> ExtractJsonFrontMatter(std::string_view source, size_t start) {
    size_t position = start;
    while (position < source.size() && (source[position] == ' ' || source[position] == '\t')) {
        ++position;
    }
    if (position >= source.size() || source[position] != '{') {
        return std::nullopt;
    }

    const size_t objectStart = position;
    int objectDepth = 0;
    bool foundMetadataKey = false;
    size_t objectEnd = std::string_view::npos;
    while (position < source.size()) {
        const char ch = source[position];
        if (ch == '{') {
            ++objectDepth;
            ++position;
            continue;
        }
        if (ch == '}') {
            --objectDepth;
            ++position;
            if (objectDepth == 0) {
                objectEnd = position;
                break;
            }
            if (objectDepth < 0) {
                return std::nullopt;
            }
            continue;
        }
        if (ch != '"') {
            ++position;
            continue;
        }

        const size_t stringStart = ++position;
        bool escaped = false;
        while (position < source.size()) {
            const char stringChar = source[position];
            if (escaped) {
                escaped = false;
            } else if (stringChar == '\\') {
                escaped = true;
            } else if (stringChar == '"') {
                break;
            }
            ++position;
        }
        if (position >= source.size()) {
            return std::nullopt;
        }
        const std::string_view value = source.substr(stringStart, position - stringStart);
        ++position;
        size_t afterString = position;
        while (afterString < source.size() && std::isspace(static_cast<unsigned char>(source[afterString]))) {
            ++afterString;
        }
        if (objectDepth == 1 && afterString < source.size() && source[afterString] == ':' &&
            IsTypicalJsonMetadataKey(value)) {
            foundMetadataKey = true;
        }
    }

    if (objectEnd == std::string_view::npos || !foundMetadataKey) {
        return std::nullopt;
    }

    size_t bodyOffset = objectEnd;
    while (bodyOffset < source.size() && (source[bodyOffset] == ' ' || source[bodyOffset] == '\t')) {
        ++bodyOffset;
    }
    if (bodyOffset < source.size() && source[bodyOffset] == '\r') {
        ++bodyOffset;
    }
    if (bodyOffset < source.size() && source[bodyOffset] == '\n') {
        ++bodyOffset;
    } else if (bodyOffset < source.size()) {
        return std::nullopt;
    }

    return FrontMatter{
        .format = "JSON",
        .content = std::string(TrimAscii(source.substr(objectStart + 1, objectEnd - objectStart - 2))),
        .bodyOffset = bodyOffset,
    };
}

std::optional<FrontMatter> ExtractFrontMatter(std::string_view source) {
    size_t start = 0;
    if (source.size() >= 3 && source.substr(0, 3) == "\xEF\xBB\xBF") {
        start = 3;
    }
    if (auto yaml = ExtractDelimitedFrontMatter(source, start, "---", "YAML", ':')) {
        return yaml;
    }
    if (auto toml = ExtractDelimitedFrontMatter(source, start, "+++", "TOML", '=')) {
        return toml;
    }
    return ExtractJsonFrontMatter(source, start);
}

std::string NormalizeMetadataKey(std::string_view key) {
    key = TrimAscii(key);
    if (key.size() >= 2 &&
        ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\''))) {
        key.remove_prefix(1);
        key.remove_suffix(1);
    }
    std::string normalized(key);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (normalized == "authors") return "author";
    return normalized;
}

std::string UnquoteMetadataValue(std::string_view value) {
    value = TrimAscii(value);
    while (!value.empty() && value.back() == ',') {
        value.remove_suffix(1);
        value = TrimAscii(value);
    }
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        const char quote = value.front();
        value.remove_prefix(1);
        value.remove_suffix(1);
        std::string unquoted;
        unquoted.reserve(value.size());
        bool escaped = false;
        for (char ch : value) {
            if (escaped) {
                switch (ch) {
                    case 'n': unquoted.push_back(' '); break;
                    case 'r': break;
                    case 't': unquoted.push_back(' '); break;
                    default: unquoted.push_back(ch); break;
                }
                escaped = false;
            } else if (ch == '\\' && quote == '"') {
                escaped = true;
            } else {
                unquoted.push_back(ch);
            }
        }
        if (escaped) {
            unquoted.push_back('\\');
        }
        return unquoted;
    }
    return std::string(value);
}

std::string CleanMetadataValue(std::string_view value) {
    value = TrimAscii(value);
    while (!value.empty() && value.back() == ',') {
        value.remove_suffix(1);
        value = TrimAscii(value);
    }
    if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
        value.remove_prefix(1);
        value.remove_suffix(1);
        std::string joined;
        size_t itemStart = 0;
        char quote = '\0';
        bool escaped = false;
        for (size_t index = 0; index <= value.size(); ++index) {
            const char ch = index < value.size() ? value[index] : ',';
            if (quote != '\0') {
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == quote) {
                    quote = '\0';
                }
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == ',') {
                const std::string item = UnquoteMetadataValue(value.substr(itemStart, index - itemStart));
                if (!item.empty()) {
                    if (!joined.empty()) joined += ", ";
                    joined += item;
                }
                itemStart = index + 1;
            }
        }
        return joined;
    }
    if (value == "|" || value == ">" || (!value.empty() && value.front() == '{')) {
        return {};
    }
    return UnquoteMetadataValue(value);
}

bool IsVisibleMetadataKey(const std::string& key) {
    return key == "title" || key == "author" || key == "date" || key == "tags";
}

std::unordered_map<std::string, std::string> ExtractVisibleMetadata(const FrontMatter& frontMatter) {
    std::unordered_map<std::string, std::string> fields;
    const char keySeparator = frontMatter.format == "TOML" ? '=' : ':';
    std::string pendingListKey;
    std::string pendingListValue;
    size_t position = 0;
    while (position < frontMatter.content.size()) {
        const auto [rawLine, next] = ReadLine(frontMatter.content, position);
        position = next;
        std::string_view line = TrimAscii(rawLine);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        if (!pendingListKey.empty()) {
            const bool startsNewKey = !rawLine.empty() &&
                !std::isspace(static_cast<unsigned char>(rawLine.front())) &&
                line.find(keySeparator) != std::string_view::npos;
            if (startsNewKey) {
                fields[pendingListKey] = pendingListValue;
                pendingListKey.clear();
                pendingListValue.clear();
            } else {
                if (line == "]" || line == "],") {
                    fields[pendingListKey] = pendingListValue;
                    pendingListKey.clear();
                    pendingListValue.clear();
                    continue;
                }
                if (line.front() == '-') {
                    line.remove_prefix(1);
                }
                const std::string item = CleanMetadataValue(line);
                if (!item.empty()) {
                    if (!pendingListValue.empty()) pendingListValue += ", ";
                    pendingListValue += item;
                }
                continue;
            }
        }

        const size_t separatorPosition = line.find(keySeparator);
        if (separatorPosition == std::string_view::npos) {
            continue;
        }
        const std::string key = NormalizeMetadataKey(line.substr(0, separatorPosition));
        if (!IsVisibleMetadataKey(key)) {
            continue;
        }
        std::string_view rawValue = TrimAscii(line.substr(separatorPosition + 1));
        const bool beginsList = !rawValue.empty() && rawValue.front() == '[';
        const bool completeList = beginsList && rawValue.find(']') != std::string_view::npos;
        if (rawValue.empty() || (beginsList && !completeList)) {
            pendingListKey = key;
            if (beginsList) {
                rawValue.remove_prefix(1);
                const std::string firstItem = CleanMetadataValue(rawValue);
                if (!firstItem.empty()) pendingListValue = firstItem;
            }
            continue;
        }
        const std::string value = CleanMetadataValue(rawValue);
        if (!value.empty()) {
            fields[key] = value;
        }
    }
    if (!pendingListKey.empty() && !pendingListValue.empty()) {
        fields[pendingListKey] = pendingListValue;
    }
    return fields;
}

Block MakeMetadataBlock(const FrontMatter& frontMatter) {
    Block block;
    block.type = BlockType::Metadata;
    block.metadataFormat = frontMatter.format;
    const auto fields = ExtractVisibleMetadata(frontMatter);
    const auto appendField = [&](std::string_view key, InlineFormatting formatting = InlineFormatting::None) {
        const auto field = fields.find(std::string(key));
        if (field == fields.end() || field->second.empty()) return false;
        block.inlineRuns.push_back(InlineRun{
            .formatting = formatting,
            .text = field->second,
        });
        return true;
    };
    bool hasPrevious = appendField("title", InlineFormatting::Strong);
    if (fields.contains("author") && !fields.at("author").empty()) {
        if (hasPrevious) {
            block.inlineRuns.push_back(InlineRun{
                .metadataRole = MetadataRunRole::DotSeparator,
                .text = "•",
            });
        }
        hasPrevious = appendField("author") || hasPrevious;
    }
    if (fields.contains("date") && !fields.at("date").empty()) {
        if (hasPrevious) {
            block.inlineRuns.push_back(InlineRun{
                .metadataRole = MetadataRunRole::Divider,
                .text = " | ",
            });
        }
        hasPrevious = appendField("date") || hasPrevious;
    }
    const auto tags = fields.find("tags");
    if (tags != fields.end() && !tags->second.empty()) {
        if (hasPrevious) {
            block.inlineRuns.push_back(InlineRun{
                .metadataRole = MetadataRunRole::Divider,
                .text = " | ",
            });
        }
        size_t start = 0;
        while (start <= tags->second.size()) {
            const size_t comma = tags->second.find(',', start);
            const size_t end = comma == std::string::npos ? tags->second.size() : comma;
            const std::string tag = std::string(TrimAscii(std::string_view(tags->second).substr(start, end - start)));
            if (!tag.empty()) {
                block.inlineRuns.push_back(InlineRun{
                    .metadataRole = MetadataRunRole::Tag,
                    .text = tag,
                });
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    return block;
}
}

struct ParserContext {
    DocumentModel doc;
    std::stack<Block*> blockStack;
    std::vector<bool> emittedBlockStack;
    std::vector<InlineFormatting> formattingStack;
    std::vector<std::string> linkTargetStack;
    struct ImageContext {
        bool active = false;
        std::string source;
    };
    std::vector<ImageContext> imageStack;
    struct InlineHtmlContext {
        std::string tagName;
        InlineFormatting formatting = InlineFormatting::None;
    };
    std::vector<InlineHtmlContext> inlineHtmlStack;
    bool failed = false;

    ParserContext() {
        formattingStack.push_back(InlineFormatting::None);
        linkTargetStack.push_back("");
        imageStack.push_back({});
        inlineHtmlStack.push_back({});
    }

    Block* CurrentBlock() {
        if (blockStack.empty()) return nullptr;
        return blockStack.top();
    }

    const Block* CurrentBlock() const {
        if (blockStack.empty()) return nullptr;
        return blockStack.top();
    }
};

static TextAlign MapTextAlign(MD_ALIGN align) {
    switch (align) {
        case MD_ALIGN_LEFT: return TextAlign::Left;
        case MD_ALIGN_CENTER: return TextAlign::Center;
        case MD_ALIGN_RIGHT: return TextAlign::Right;
        case MD_ALIGN_DEFAULT:
        default:
            return TextAlign::Default;
    }
}

static BlockType MapBlockType(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
        case MD_BLOCK_P: return BlockType::Paragraph;
        case MD_BLOCK_QUOTE: return BlockType::Blockquote;
        case MD_BLOCK_UL: return BlockType::UnorderedList;
        case MD_BLOCK_OL: return BlockType::OrderedList;
        case MD_BLOCK_LI: return BlockType::ListItem;
        case MD_BLOCK_HR: return BlockType::ThematicBreak;
        case MD_BLOCK_CODE: return BlockType::CodeBlock;
        case MD_BLOCK_TABLE: return BlockType::Table;
        case MD_BLOCK_TR: return BlockType::TableRow;
        case MD_BLOCK_TH: return BlockType::TableHeaderCell;
        case MD_BLOCK_TD: return BlockType::TableCell;
        case MD_BLOCK_HTML: return BlockType::RawHtml;
        case MD_BLOCK_H: {
            auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            switch (h->level) {
                case 1: return BlockType::Heading1;
                case 2: return BlockType::Heading2;
                case 3: return BlockType::Heading3;
                case 4: return BlockType::Heading4;
                case 5: return BlockType::Heading5;
                case 6: return BlockType::Heading6;
                default: return BlockType::Heading1;
            }
        }
        default: return BlockType::Paragraph;
    }
}

static void AppendUtf8(std::string& output, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

static std::string DecodeEntity(std::string_view entity) {
    if (entity == "&amp;") return "&";
    if (entity == "&lt;") return "<";
    if (entity == "&gt;") return ">";
    if (entity == "&quot;") return "\"";
    if (entity == "&apos;") return "'";
    if (entity == "&nbsp;") return "\xC2\xA0";

    if (entity.size() >= 4 && entity[0] == '&' && entity[1] == '#') {
        const bool isHex = entity.size() >= 5 && (entity[2] == 'x' || entity[2] == 'X');
        const size_t numberStart = isHex ? 3 : 2;
        const size_t numberEnd = entity.size() - 1;
        if (entity.back() == ';' && numberStart < numberEnd) {
            std::string number(entity.substr(numberStart, numberEnd - numberStart));
            char* parseEnd = nullptr;
            const unsigned long value = std::strtoul(number.c_str(), &parseEnd, isHex ? 16 : 10);
            if (parseEnd != number.c_str() && *parseEnd == '\0' && value <= 0x10FFFF) {
                std::string decoded;
                AppendUtf8(decoded, static_cast<unsigned>(value));
                if (!decoded.empty()) {
                    return decoded;
                }
            }
        }
    }

    return std::string(entity);
}

static std::string AttributeToString(const MD_ATTRIBUTE& attribute) {
    std::string value;
    if (attribute.text == nullptr || attribute.size == 0) {
        return value;
    }
    if (attribute.substr_types == nullptr || attribute.substr_offsets == nullptr) {
        return std::string(attribute.text, attribute.size);
    }

    for (MD_SIZE partIndex = 0; attribute.substr_offsets[partIndex] < attribute.size; ++partIndex) {
        const MD_TEXTTYPE textType = attribute.substr_types[partIndex];
        const MD_OFFSET start = attribute.substr_offsets[partIndex];
        const MD_OFFSET end = attribute.substr_offsets[partIndex + 1];
        const std::string_view part(attribute.text + start, end - start);
        value += textType == MD_TEXT_ENTITY ? DecodeEntity(part) : std::string(part);
    }
    return value;
}

static InlineFormatting FormattingForSpan(MD_SPANTYPE type) {
    switch (type) {
        case MD_SPAN_EM: return InlineFormatting::Emphasis;
        case MD_SPAN_STRONG: return InlineFormatting::Strong;
        case MD_SPAN_CODE: return InlineFormatting::Code;
        case MD_SPAN_DEL: return InlineFormatting::Strikethrough;
        default: return InlineFormatting::None;
    }
}

static bool ShouldSkipBlock(MD_BLOCKTYPE type, const ParserContext& ctx) {
    if (type == MD_BLOCK_THEAD || type == MD_BLOCK_TBODY) {
        return true;
    }

    if (type == MD_BLOCK_P) {
        const Block* currentBlock = ctx.CurrentBlock();
        if (currentBlock != nullptr &&
            (currentBlock->type == BlockType::TableHeaderCell || currentBlock->type == BlockType::TableCell)) {
            return true;
        }
    }

    return false;
}

static int EnterBlockImpl(MD_BLOCKTYPE type, void* detail, void* userdata) {
    if (type == MD_BLOCK_DOC) {
        return 0;
    }

    auto* ctx = static_cast<ParserContext*>(userdata);
    if (ctx->blockStack.size() >= kMaxMarkdownNestingDepth) {
        ctx->failed = true;
        return 1;
    }
    if (ShouldSkipBlock(type, *ctx)) {
        ctx->emittedBlockStack.push_back(false);
        return 0;
    }
    
    Block block;
    block.type = MapBlockType(type, detail);
    if (type == MD_BLOCK_OL) {
        const auto* olDetail = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        if (olDetail != nullptr) {
            block.orderedListStart = olDetail->start;
            block.orderedListDelimiter = olDetail->mark_delimiter;
        }
    }
    if (type == MD_BLOCK_LI) {
        const auto* liDetail = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        if (liDetail != nullptr && liDetail->is_task) {
            block.taskListState =
                (liDetail->task_mark == 'x' || liDetail->task_mark == 'X')
                    ? TaskListState::Checked
                    : TaskListState::Unchecked;
        }
    }
    if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
        const auto* cellDetail = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
        block.align = MapTextAlign(cellDetail->align);
    }
    if (type == MD_BLOCK_CODE) {
        const auto* codeDetail = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        if (codeDetail != nullptr) {
            block.codeLanguage = AttributeToString(codeDetail->lang);
        }
    }

    if (ctx->blockStack.empty()) {
        ctx->doc.blocks.push_back(std::move(block));
        ctx->blockStack.push(&ctx->doc.blocks.back());
    } else {
        auto* parent = ctx->blockStack.top();
        parent->children.push_back(std::move(block));
        ctx->blockStack.push(&parent->children.back());
    }
    ctx->emittedBlockStack.push_back(true);
    return 0;
}

static int LeaveBlockImpl(MD_BLOCKTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    if (type == MD_BLOCK_DOC) {
        return 0;
    }

    auto* ctx = static_cast<ParserContext*>(userdata);
    if (!ctx->emittedBlockStack.empty() && ctx->emittedBlockStack.back() && !ctx->blockStack.empty()) {
        ctx->blockStack.pop();
    }
    if (!ctx->emittedBlockStack.empty()) {
        ctx->emittedBlockStack.pop_back();
    }
    if (ctx->inlineHtmlStack.size() > 1) {
        ctx->inlineHtmlStack.resize(1);
    }
    return 0;
}

static int EnterSpanImpl(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    if (ctx->formattingStack.size() >= kMaxMarkdownNestingDepth) {
        ctx->failed = true;
        return 1;
    }
    InlineFormatting formatting = ctx->formattingStack.back();
    formatting |= FormattingForSpan(type);
    ctx->formattingStack.push_back(formatting);

    std::string linkTarget = ctx->linkTargetStack.back();
    if (type == MD_SPAN_A) {
        auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
        linkTarget = AttributeToString(a->href);
    }
    ctx->linkTargetStack.push_back(std::move(linkTarget));

    ParserContext::ImageContext image = ctx->imageStack.back();
    if (type == MD_SPAN_IMG) {
        auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        image.active = true;
        image.source = AttributeToString(img->src);
    }
    ctx->imageStack.push_back(std::move(image));

    return 0;
}

struct HtmlTag {
    std::string name;
    std::unordered_map<std::string, std::string> attributes;
    bool closing = false;
    bool selfClosing = false;
    size_t end = 0;
};

static std::string LowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

static void SkipHtmlWhitespace(std::string_view html, size_t& position) {
    while (position < html.size() && std::isspace(static_cast<unsigned char>(html[position]))) {
        ++position;
    }
}

static bool IsHtmlNameCharacter(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) || ch == '-' || ch == '_';
}

static std::optional<HtmlTag> ParseHtmlTag(std::string_view html, size_t position) {
    if (position >= html.size() || html[position] != '<') {
        return std::nullopt;
    }

    HtmlTag tag;
    ++position;
    if (position < html.size() && html[position] == '/') {
        tag.closing = true;
        ++position;
    }
    SkipHtmlWhitespace(html, position);
    const size_t nameStart = position;
    while (position < html.size() && IsHtmlNameCharacter(html[position])) {
        ++position;
    }
    if (position == nameStart) {
        return std::nullopt;
    }
    tag.name = LowerAscii(html.substr(nameStart, position - nameStart));

    while (position < html.size()) {
        SkipHtmlWhitespace(html, position);
        if (position >= html.size()) {
            return std::nullopt;
        }
        if (html[position] == '>') {
            tag.end = position + 1;
            return tag;
        }
        if (html[position] == '/' && position + 1 < html.size() && html[position + 1] == '>') {
            tag.selfClosing = true;
            tag.end = position + 2;
            return tag;
        }
        if (tag.closing) {
            return std::nullopt;
        }

        const size_t attributeNameStart = position;
        while (position < html.size() && IsHtmlNameCharacter(html[position])) {
            ++position;
        }
        if (position == attributeNameStart) {
            return std::nullopt;
        }
        const std::string attributeName = LowerAscii(html.substr(attributeNameStart, position - attributeNameStart));
        SkipHtmlWhitespace(html, position);

        std::string value;
        if (position < html.size() && html[position] == '=') {
            ++position;
            SkipHtmlWhitespace(html, position);
            if (position >= html.size()) {
                return std::nullopt;
            }
            if (html[position] == '\'' || html[position] == '"') {
                const char quote = html[position++];
                const size_t valueStart = position;
                while (position < html.size() && html[position] != quote) {
                    ++position;
                }
                if (position >= html.size()) {
                    return std::nullopt;
                }
                value.assign(html.substr(valueStart, position - valueStart));
                ++position;
            } else {
                const size_t valueStart = position;
                while (position < html.size() && !std::isspace(static_cast<unsigned char>(html[position])) &&
                       html[position] != '>' && html[position] != '/') {
                    ++position;
                }
                value.assign(html.substr(valueStart, position - valueStart));
            }
        }
        tag.attributes[attributeName] = std::move(value);
    }
    return std::nullopt;
}

static std::string DecodeHtmlText(std::string_view text) {
    std::string decoded;
    bool inWhitespace = false;
    for (size_t index = 0; index < text.size();) {
        if (std::isspace(static_cast<unsigned char>(text[index]))) {
            if (!inWhitespace) {
                decoded.push_back(' ');
                inWhitespace = true;
            }
            ++index;
            continue;
        }
        inWhitespace = false;
        if (text[index] == '&') {
            const size_t semicolon = text.find(';', index + 1);
            if (semicolon != std::string_view::npos && semicolon - index <= 12) {
                decoded += DecodeEntity(text.substr(index, semicolon - index + 1));
                index = semicolon + 1;
                continue;
            }
        }
        decoded.push_back(text[index++]);
    }
    return decoded;
}

static bool IsSafeHtmlReference(std::string_view reference, bool image) {
    if (reference.empty() || reference.find_first_of("\r\n\0") != std::string_view::npos) {
        return false;
    }
    const std::string lowered = LowerAscii(reference);
    if (lowered.starts_with("javascript:") || lowered.starts_with("data:") ||
        lowered.starts_with("vbscript:") || lowered.starts_with("file:")) {
        return false;
    }
    if (image && lowered.starts_with("mailto:")) {
        return false;
    }
    return true;
}

static float ParseHtmlDimension(const std::unordered_map<std::string, std::string>& attributes, const char* name) {
    const auto found = attributes.find(name);
    if (found == attributes.end() || found->second.empty()) {
        return 0.0f;
    }
    char* end = nullptr;
    const float value = std::strtof(found->second.c_str(), &end);
    if (end == found->second.c_str() || *end != '\0' || !std::isfinite(value) || value <= 0.0f) {
        return 0.0f;
    }
    return std::min(value, 8192.0f);
}

static TextAlign ParseHtmlAlign(const HtmlTag& tag) {
    const auto found = tag.attributes.find("align");
    if (found == tag.attributes.end()) return TextAlign::Default;
    const std::string value = LowerAscii(found->second);
    if (value == "center") return TextAlign::Center;
    if (value == "right") return TextAlign::Right;
    if (value == "left") return TextAlign::Left;
    return TextAlign::Default;
}

static bool HasOnlyAttributes(const HtmlTag& tag, std::initializer_list<std::string_view> allowed) {
    for (const auto& [name, value] : tag.attributes) {
        (void)value;
        if (std::none_of(allowed.begin(), allowed.end(), [&](std::string_view item) { return item == name; })) {
            return false;
        }
    }
    return true;
}

static InlineFormatting FormattingForSafeHtmlTag(std::string_view name) {
    if (name == "kbd") return InlineFormatting::Keyboard;
    if (name == "sub") return InlineFormatting::Subscript;
    if (name == "sup") return InlineFormatting::Superscript;
    return InlineFormatting::None;
}

static bool HandleSafeInlineHtmlTag(ParserContext& ctx, std::string_view html) {
    const auto tag = ParseHtmlTag(html, 0);
    if (!tag || tag->end != html.size() || tag->selfClosing || !HasOnlyAttributes(*tag, {})) {
        return false;
    }
    const InlineFormatting addedFormatting = FormattingForSafeHtmlTag(tag->name);
    if (addedFormatting == InlineFormatting::None) {
        return false;
    }
    if (tag->closing) {
        if (ctx.inlineHtmlStack.size() <= 1 || ctx.inlineHtmlStack.back().tagName != tag->name) {
            return false;
        }
        ctx.inlineHtmlStack.pop_back();
        return true;
    }
    if (ctx.inlineHtmlStack.size() >= kMaxMarkdownNestingDepth) {
        ctx.failed = true;
        return true;
    }
    ctx.inlineHtmlStack.push_back(ParserContext::InlineHtmlContext{
        .tagName = tag->name,
        .formatting = ctx.inlineHtmlStack.back().formatting | addedFormatting,
    });
    return true;
}

static void AppendHtmlRun(Block& block, InlineRun run) {
    if (run.kind == InlineKind::Text && run.text.empty()) {
        return;
    }
    if (!block.inlineRuns.empty() && run.kind == InlineKind::Text &&
        block.inlineRuns.back().kind == InlineKind::Text &&
        block.inlineRuns.back().formatting == run.formatting &&
        block.inlineRuns.back().linkTarget == run.linkTarget) {
        block.inlineRuns.back().text += run.text;
        return;
    }
    block.inlineRuns.push_back(std::move(run));
}

static std::optional<Block> ParseSafeHtmlBlock(std::string_view html) {
    size_t position = 0;
    SkipHtmlWhitespace(html, position);
    const auto root = ParseHtmlTag(html, position);
    if (!root || root->closing || root->selfClosing || !HasOnlyAttributes(*root, {"align"})) {
        return std::nullopt;
    }

    Block block;
    if (root->name == "p") block.type = BlockType::Paragraph;
    else if (root->name == "h1") block.type = BlockType::Heading1;
    else if (root->name == "h2") block.type = BlockType::Heading2;
    else if (root->name == "h3") block.type = BlockType::Heading3;
    else if (root->name == "h4") block.type = BlockType::Heading4;
    else if (root->name == "h5") block.type = BlockType::Heading5;
    else if (root->name == "h6") block.type = BlockType::Heading6;
    else return std::nullopt;
    block.align = ParseHtmlAlign(*root);
    position = root->end;

    std::vector<std::string> linkStack(1);
    std::vector<std::pair<std::string, InlineFormatting>> formattingStack{
        {std::string{}, InlineFormatting::None},
    };
    bool closedRoot = false;
    while (position < html.size()) {
        if (html[position] != '<') {
            const size_t tagStart = html.find('<', position);
            const size_t textEnd = tagStart == std::string_view::npos ? html.size() : tagStart;
            AppendHtmlRun(block, InlineRun{
                .formatting = formattingStack.back().second,
                .text = DecodeHtmlText(html.substr(position, textEnd - position)),
                .linkTarget = linkStack.back(),
            });
            position = textEnd;
            continue;
        }

        const auto tag = ParseHtmlTag(html, position);
        if (!tag) return std::nullopt;
        position = tag->end;
        if (tag->closing && tag->name == root->name && linkStack.size() == 1 && formattingStack.size() == 1) {
            closedRoot = true;
            break;
        }
        if (tag->name == "a") {
            if (!HasOnlyAttributes(*tag, {"href"})) return std::nullopt;
            if (tag->closing) {
                if (linkStack.size() <= 1) return std::nullopt;
                linkStack.pop_back();
            } else {
                const auto href = tag->attributes.find("href");
                if (href == tag->attributes.end() || !IsSafeHtmlReference(href->second, false)) return std::nullopt;
                linkStack.push_back(href->second);
            }
            continue;
        }
        const InlineFormatting addedFormatting = FormattingForSafeHtmlTag(tag->name);
        if (addedFormatting != InlineFormatting::None && HasOnlyAttributes(*tag, {}) && !tag->selfClosing) {
            if (tag->closing) {
                if (formattingStack.size() <= 1 || formattingStack.back().first != tag->name) return std::nullopt;
                formattingStack.pop_back();
            } else {
                formattingStack.push_back({tag->name, formattingStack.back().second | addedFormatting});
            }
            continue;
        }
        if (!tag->closing && tag->name == "br" && HasOnlyAttributes(*tag, {})) {
            AppendHtmlRun(block, InlineRun{.kind = InlineKind::HardBreak});
            continue;
        }
        if (!tag->closing && tag->name == "img" && HasOnlyAttributes(*tag, {"src", "alt", "width", "height"})) {
            const auto source = tag->attributes.find("src");
            if (source == tag->attributes.end() || !IsSafeHtmlReference(source->second, true)) return std::nullopt;
            const auto alt = tag->attributes.find("alt");
            AppendHtmlRun(block, InlineRun{
                .formatting = formattingStack.back().second,
                .kind = InlineKind::Image,
                .text = alt == tag->attributes.end() ? std::string{} : DecodeHtmlText(alt->second),
                .imageSource = source->second,
                .linkTarget = linkStack.back(),
                .imageRequestedWidth = ParseHtmlDimension(tag->attributes, "width"),
                .imageRequestedHeight = ParseHtmlDimension(tag->attributes, "height"),
            });
            continue;
        }
        return std::nullopt;
    }

    SkipHtmlWhitespace(html, position);
    if (!closedRoot || position != html.size() || linkStack.size() != 1 || formattingStack.size() != 1) {
        return std::nullopt;
    }
    if (!block.inlineRuns.empty() && block.inlineRuns.front().kind == InlineKind::Text) {
        auto& text = block.inlineRuns.front().text;
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](unsigned char ch) { return !std::isspace(ch); }));
        if (text.empty()) block.inlineRuns.erase(block.inlineRuns.begin());
    }
    if (!block.inlineRuns.empty() && block.inlineRuns.back().kind == InlineKind::Text) {
        auto& text = block.inlineRuns.back().text;
        text.erase(std::find_if(text.rbegin(), text.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), text.end());
        if (text.empty()) block.inlineRuns.pop_back();
    }
    return block;
}

struct DetailsOpening {
    std::vector<InlineRun> summaryRuns;
    bool open = false;
};

static std::optional<DetailsOpening> ParseDetailsOpening(std::string_view html) {
    size_t position = 0;
    SkipHtmlWhitespace(html, position);
    const auto details = ParseHtmlTag(html, position);
    if (!details || details->closing || details->selfClosing || details->name != "details" ||
        !HasOnlyAttributes(*details, {"open"})) {
        return std::nullopt;
    }
    position = details->end;
    SkipHtmlWhitespace(html, position);
    const auto summary = ParseHtmlTag(html, position);
    if (!summary || summary->closing || summary->selfClosing || summary->name != "summary" ||
        !HasOnlyAttributes(*summary, {})) {
        return std::nullopt;
    }

    const size_t summaryContentStart = summary->end;
    const std::string loweredTail = LowerAscii(html.substr(summaryContentStart));
    const size_t relativeClosingStart = loweredTail.find("</summary");
    if (relativeClosingStart == std::string::npos) {
        return std::nullopt;
    }
    const size_t closingStart = summaryContentStart + relativeClosingStart;
    const auto closingSummary = ParseHtmlTag(html, closingStart);
    if (!closingSummary || !closingSummary->closing || closingSummary->name != "summary" ||
        !HasOnlyAttributes(*closingSummary, {})) {
        return std::nullopt;
    }
    position = closingSummary->end;
    SkipHtmlWhitespace(html, position);
    if (position != html.size()) {
        return std::nullopt;
    }

    const std::string summaryHtml = "<p>" +
        std::string(html.substr(summaryContentStart, closingStart - summaryContentStart)) +
        "</p>";
    auto summaryBlock = ParseSafeHtmlBlock(summaryHtml);
    if (!summaryBlock) {
        return std::nullopt;
    }
    if (summaryBlock->inlineRuns.empty()) {
        summaryBlock->inlineRuns.push_back(InlineRun{.text = "Details"});
    }
    for (auto& run : summaryBlock->inlineRuns) {
        run.formatting |= InlineFormatting::Strong;
    }
    return DetailsOpening{
        .summaryRuns = std::move(summaryBlock->inlineRuns),
        .open = details->attributes.contains("open"),
    };
}

static bool IsDetailsClosing(std::string_view html) {
    size_t position = 0;
    SkipHtmlWhitespace(html, position);
    const auto tag = ParseHtmlTag(html, position);
    if (!tag || !tag->closing || tag->name != "details" || !HasOnlyAttributes(*tag, {})) {
        return false;
    }
    position = tag->end;
    SkipHtmlWhitespace(html, position);
    return position == html.size();
}

static void ExpandSafeHtmlBlocks(std::vector<Block>& blocks, size_t& nextDetailsId) {
    for (size_t index = 0; index < blocks.size();) {
        if (blocks[index].type == BlockType::RawHtml) {
            if (auto opening = ParseDetailsOpening(blocks[index].rawHtml)) {
                size_t depth = 1;
                size_t closingIndex = index + 1;
                for (; closingIndex < blocks.size(); ++closingIndex) {
                    if (blocks[closingIndex].type != BlockType::RawHtml) {
                        continue;
                    }
                    if (ParseDetailsOpening(blocks[closingIndex].rawHtml)) {
                        ++depth;
                    } else if (IsDetailsClosing(blocks[closingIndex].rawHtml) && --depth == 0) {
                        break;
                    }
                }
                if (closingIndex < blocks.size()) {
                    Block details;
                    details.type = BlockType::Details;
                    details.inlineRuns = std::move(opening->summaryRuns);
                    details.detailsId = nextDetailsId++;
                    details.detailsOpen = opening->open;
                    details.children.reserve(closingIndex - index - 1);
                    for (size_t childIndex = index + 1; childIndex < closingIndex; ++childIndex) {
                        details.children.push_back(std::move(blocks[childIndex]));
                    }
                    ExpandSafeHtmlBlocks(details.children, nextDetailsId);
                    blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index),
                                 blocks.begin() + static_cast<std::ptrdiff_t>(closingIndex + 1));
                    blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(index), std::move(details));
                    ++index;
                    continue;
                }
            }
        }

        Block& block = blocks[index];
        if (!block.children.empty()) {
            ExpandSafeHtmlBlocks(block.children, nextDetailsId);
        }
        if (block.type == BlockType::RawHtml) {
            if (auto parsed = ParseSafeHtmlBlock(block.rawHtml)) {
                block = std::move(*parsed);
            } else {
                block.inlineRuns = {InlineRun{.text = block.rawHtml}};
            }
        }
        ++index;
    }
}

static AlertKind AlertKindFromMarker(std::string_view marker) {
    const std::string normalized = LowerAscii(marker);
    if (normalized == "[!note]") return AlertKind::Note;
    if (normalized == "[!tip]") return AlertKind::Tip;
    if (normalized == "[!important]") return AlertKind::Important;
    if (normalized == "[!warning]") return AlertKind::Warning;
    if (normalized == "[!caution]") return AlertKind::Caution;
    return AlertKind::None;
}

static AlertKind RemoveAlertMarker(Block& paragraph) {
    if (paragraph.type != BlockType::Paragraph || paragraph.inlineRuns.empty() ||
        paragraph.inlineRuns.front().kind != InlineKind::Text) {
        return AlertKind::None;
    }

    std::string marker = paragraph.inlineRuns.front().text;
    marker.erase(marker.begin(), std::find_if(marker.begin(), marker.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    marker.erase(std::find_if(marker.rbegin(), marker.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), marker.end());
    const AlertKind kind = AlertKindFromMarker(marker);
    if (kind == AlertKind::None) {
        return AlertKind::None;
    }

    paragraph.inlineRuns.erase(paragraph.inlineRuns.begin());
    if (!paragraph.inlineRuns.empty() &&
        (paragraph.inlineRuns.front().kind == InlineKind::SoftBreak ||
         paragraph.inlineRuns.front().kind == InlineKind::HardBreak)) {
        paragraph.inlineRuns.erase(paragraph.inlineRuns.begin());
    }
    return kind;
}

static void ExpandGithubAlerts(std::vector<Block>& blocks) {
    for (auto& block : blocks) {
        if (!block.children.empty()) {
            ExpandGithubAlerts(block.children);
        }
        if (block.type != BlockType::Blockquote || block.children.empty()) {
            continue;
        }

        block.alertKind = RemoveAlertMarker(block.children.front());
        if (block.alertKind != AlertKind::None &&
            block.children.front().inlineRuns.empty() &&
            block.children.front().children.empty()) {
            block.children.erase(block.children.begin());
        }
    }
}

static int LeaveSpanImpl(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    auto* ctx = static_cast<ParserContext*>(userdata);
    if (ctx->formattingStack.size() > 1) {
        ctx->formattingStack.pop_back();
    }
    if (ctx->linkTargetStack.size() > 1) {
        ctx->linkTargetStack.pop_back();
    }
    if (ctx->imageStack.size() > 1) {
        ctx->imageStack.pop_back();
    }
    return 0;
}

static int TextImpl(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    auto* currentBlock = ctx->CurrentBlock();
    if (!currentBlock) return 0;

    if (currentBlock->type == BlockType::RawHtml) {
        currentBlock->rawHtml.append(text, size);
        return 0;
    }

    InlineKind kind = InlineKind::Text;
    std::string str;
    switch (type) {
        case MD_TEXT_BR:
            kind = InlineKind::HardBreak;
            break;
        case MD_TEXT_SOFTBR:
            kind = InlineKind::SoftBreak;
            break;
        case MD_TEXT_NULLCHAR:
            str = "\xEF\xBF\xBD";
            break;
        case MD_TEXT_ENTITY:
            str = DecodeEntity(std::string_view(text, size));
            break;
        case MD_TEXT_HTML: {
            const std::string lowered = LowerAscii(std::string_view(text, size));
            if (lowered == "<br>" || lowered == "<br/>" || lowered == "<br />") {
                kind = InlineKind::HardBreak;
            } else if (HandleSafeInlineHtmlTag(*ctx, std::string_view(text, size))) {
                return ctx->failed ? 1 : 0;
            } else {
                str.assign(text, size);
            }
            break;
        }
        default:
            str.assign(text, size);
            break;
    }

    const ParserContext::ImageContext& image = ctx->imageStack.back();
    if (kind == InlineKind::Text && image.active) {
        kind = InlineKind::Image;
    }

    if (!currentBlock->inlineRuns.empty() &&
        kind != InlineKind::SoftBreak &&
        kind != InlineKind::HardBreak &&
        currentBlock->inlineRuns.back().formatting ==
            (ctx->formattingStack.back() | ctx->inlineHtmlStack.back().formatting) &&
        currentBlock->inlineRuns.back().kind == kind &&
        currentBlock->inlineRuns.back().syntaxRole == SyntaxRole::None &&
        currentBlock->inlineRuns.back().imageSource == (image.active ? image.source : std::string{}) &&
        currentBlock->inlineRuns.back().linkTarget == ctx->linkTargetStack.back()) {
        currentBlock->inlineRuns.back().text += str;
    } else {
        currentBlock->inlineRuns.push_back(InlineRun{
            .formatting = ctx->formattingStack.back() | ctx->inlineHtmlStack.back().formatting,
            .kind = kind,
            .syntaxRole = SyntaxRole::None,
            .text = std::move(str),
            .imageSource = image.active ? image.source : std::string{},
            .linkTarget = ctx->linkTargetStack.back(),
        });
    }

    return 0;
}

template <typename Callback>
static int GuardParserCallback(void* userdata, Callback&& callback) noexcept {
    auto* ctx = static_cast<ParserContext*>(userdata);
    try {
        return callback();
    } catch (...) {
        ctx->failed = true;
        return 1;
    }
}

static int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) noexcept {
    return GuardParserCallback(userdata, [&] { return EnterBlockImpl(type, detail, userdata); });
}

static int LeaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) noexcept {
    return GuardParserCallback(userdata, [&] { return LeaveBlockImpl(type, detail, userdata); });
}

static int EnterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) noexcept {
    return GuardParserCallback(userdata, [&] { return EnterSpanImpl(type, detail, userdata); });
}

static int LeaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) noexcept {
    return GuardParserCallback(userdata, [&] { return LeaveSpanImpl(type, detail, userdata); });
}

static int TextCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) noexcept {
    return GuardParserCallback(userdata, [&] { return TextImpl(type, text, size, userdata); });
}

DocumentModel MarkdownParser::Parse(const std::string& source) {
    const std::optional<FrontMatter> frontMatter = ExtractFrontMatter(source);
    const std::string_view markdownSource = frontMatter
        ? std::string_view(source).substr(frontMatter->bodyOffset)
        : std::string_view(source);
    ParserContext ctx;
    MD_PARSER parser = {0};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS;
    parser.enter_block = EnterBlockCallback;
    parser.leave_block = LeaveBlockCallback;
    parser.enter_span = EnterSpanCallback;
    parser.leave_span = LeaveSpanCallback;
    parser.text = TextCallback;

    const int parseStatus = md_parse(
        markdownSource.data(),
        static_cast<MD_SIZE>(markdownSource.size()),
        &parser,
        &ctx);
    if (parseStatus != 0 || ctx.failed) {
        return {};
    }

    size_t nextDetailsId = 1;
    ExpandSafeHtmlBlocks(ctx.doc.blocks, nextDetailsId);
    ExpandGithubAlerts(ctx.doc.blocks);
    if (frontMatter) {
        Block metadata = MakeMetadataBlock(*frontMatter);
        if (!metadata.inlineRuns.empty()) {
            ctx.doc.blocks.insert(ctx.doc.blocks.begin(), std::move(metadata));
        }
    }
    return std::move(ctx.doc);
}

} // namespace mdviewer
