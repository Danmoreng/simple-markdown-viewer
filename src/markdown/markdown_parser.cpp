#include "markdown_parser.h"
#include "md4c.h"
#include <cctype>
#include <stack>
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

static InlineStyle MapSpanType(MD_SPANTYPE type) {
    switch (type) {
        case MD_SPAN_EM: return InlineStyle::Emphasis;
        case MD_SPAN_STRONG: return InlineStyle::Strong;
        case MD_SPAN_CODE: return InlineStyle::Code;
        case MD_SPAN_A: return InlineStyle::Link;
        case MD_SPAN_IMG: return InlineStyle::Image;
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
    if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
        const auto* cellDetail = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
        block.align = MapTextAlign(cellDetail->align);
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
        url.assign(a->href.text, a->href.size);
    } else if (type == MD_SPAN_IMG) {
        auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        url.assign(img->src.text, img->src.size);
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
