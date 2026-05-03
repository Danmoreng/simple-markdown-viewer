#include "markdown_parser.h"
#include "md4c.h"
#include <cctype>
#include <cstdlib>
#include <stack>
#include <string_view>
#include <vector>

namespace mdviewer {

struct ParserContext {
    DocumentModel doc;
    std::stack<Block*> blockStack;
    std::vector<bool> emittedBlockStack;
    std::vector<InlineStyle> styleStack;
    std::vector<std::string> urlStack;

    ParserContext() {
        styleStack.push_back(InlineStyle::Plain);
        urlStack.push_back("");
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

static std::string NormalizeMarkdown(const std::string& source) {
    auto normalizeLine = [](std::string_view line) {
        std::string normalized(line);
        size_t indent = 0;
        while (indent < normalized.size() && (normalized[indent] == ' ' || normalized[indent] == '\t')) {
            indent++;
        }

        if (indent >= normalized.size()) {
            return normalized;
        }

        auto needsSpace = [&](size_t index) {
            return index < normalized.size() &&
                   normalized[index] != ' ' &&
                   normalized[index] != '\t';
        };

        auto hasUnescapedPipe = [&]() {
            bool escaped = false;
            for (size_t index = indent; index < normalized.size(); ++index) {
                const char ch = normalized[index];
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (ch == '\\') {
                    escaped = true;
                    continue;
                }
                if (ch == '|') {
                    return true;
                }
            }
            return false;
        };

        if (normalized[indent] == '>') {
            if (needsSpace(indent + 1)) {
                normalized.insert(indent + 1, 1, ' ');
            }
            return normalized;
        }

        size_t hashCount = 0;
        while (indent + hashCount < normalized.size() &&
               normalized[indent + hashCount] == '#' &&
               hashCount < 6) {
            hashCount++;
        }
        if (hashCount > 0 && needsSpace(indent + hashCount)) {
            normalized.insert(indent + hashCount, 1, ' ');
            return normalized;
        }

        // Do not reinterpret optional-pipe table rows like
        // "*Still* | `renders` | **nicely**" as malformed list items.
        if (hasUnescapedPipe()) {
            return normalized;
        }

        if ((normalized[indent] == '*' || normalized[indent] == '-' || normalized[indent] == '+') &&
            indent + 1 < normalized.size() &&
            normalized[indent + 1] != normalized[indent] &&
            needsSpace(indent + 1)) {
            normalized.insert(indent + 1, 1, ' ');
            return normalized;
        }

        size_t numberEnd = indent;
        while (numberEnd < normalized.size() &&
               std::isdigit(static_cast<unsigned char>(normalized[numberEnd]))) {
            numberEnd++;
        }
        if (numberEnd > indent &&
            numberEnd < normalized.size() &&
            (normalized[numberEnd] == '.' || normalized[numberEnd] == ')') &&
            needsSpace(numberEnd + 1)) {
            normalized.insert(numberEnd + 1, 1, ' ');
        }

        return normalized;
    };

    std::string normalized;
    normalized.reserve(source.size() + 16);

    size_t lineStart = 0;
    while (lineStart < source.size()) {
        size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = source.size();
        }

        size_t contentEnd = lineEnd;
        bool hasCarriageReturn = contentEnd > lineStart && source[contentEnd - 1] == '\r';
        if (hasCarriageReturn) {
            contentEnd--;
        }

        normalized += normalizeLine(std::string_view(source.data() + lineStart, contentEnd - lineStart));
        if (hasCarriageReturn) {
            normalized += '\r';
        }
        if (lineEnd < source.size()) {
            normalized += '\n';
        }

        lineStart = lineEnd + 1;
    }

    return normalized;
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

static InlineStyle MapSpanType(MD_SPANTYPE type) {
    switch (type) {
        case MD_SPAN_EM: return InlineStyle::Emphasis;
        case MD_SPAN_STRONG: return InlineStyle::Strong;
        case MD_SPAN_CODE: return InlineStyle::Code;
        case MD_SPAN_A: return InlineStyle::Link;
        case MD_SPAN_IMG: return InlineStyle::Image;
        case MD_SPAN_DEL: return InlineStyle::Strikethrough;
        default: return InlineStyle::Plain;
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

static int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
    if (type == MD_BLOCK_DOC) {
        return 0;
    }

    auto* ctx = static_cast<ParserContext*>(userdata);
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

static int LeaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
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
    return 0;
}

static int EnterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    InlineStyle style = MapSpanType(type);
    ctx->styleStack.push_back(style);

    std::string url;
    if (type == MD_SPAN_A) {
        auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
        url = AttributeToString(a->href);
    } else if (type == MD_SPAN_IMG) {
        auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        url = AttributeToString(img->src);
    }
    ctx->urlStack.push_back(url);

    return 0;
}

static int LeaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    auto* ctx = static_cast<ParserContext*>(userdata);
    if (ctx->styleStack.size() > 1) {
        ctx->styleStack.pop_back();
    }
    if (ctx->urlStack.size() > 1) {
        ctx->urlStack.pop_back();
    }
    return 0;
}

static int TextCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    auto* currentBlock = ctx->CurrentBlock();
    if (!currentBlock) return 0;

    std::string str;
    switch (type) {
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            str = "\n";
            break;
        case MD_TEXT_NULLCHAR:
            str = "\xEF\xBF\xBD";
            break;
        case MD_TEXT_ENTITY:
            str = DecodeEntity(std::string_view(text, size));
            break;
        default:
            str.assign(text, size);
            break;
    }

    InlineStyle currentStyle = ctx->styleStack.back();
    std::string currentUrl = ctx->urlStack.back();

    if (!currentBlock->inlineRuns.empty() && 
        currentBlock->inlineRuns.back().style == currentStyle &&
        currentBlock->inlineRuns.back().url == currentUrl) {
        currentBlock->inlineRuns.back().text += str;
    } else {
        currentBlock->inlineRuns.push_back({currentStyle, str, currentUrl});
    }

    return 0;
}

DocumentModel MarkdownParser::Parse(const std::string& source) {
    ParserContext ctx;
    const std::string normalizedSource = NormalizeMarkdown(source);
    MD_PARSER parser = {0};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS;
    parser.enter_block = EnterBlockCallback;
    parser.leave_block = LeaveBlockCallback;
    parser.enter_span = EnterSpanCallback;
    parser.leave_span = LeaveSpanCallback;
    parser.text = TextCallback;

    md_parse(normalizedSource.c_str(), static_cast<MD_SIZE>(normalizedSource.size()), &parser, &ctx);

    return std::move(ctx.doc);
}

} // namespace mdviewer
