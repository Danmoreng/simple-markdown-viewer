#include "markdown_parser.h"
#include "md4c.h"
#include <stack>
#include <vector>

namespace mdviewer {

struct ParserContext {
    DocumentModel doc;
    std::stack<Block*> blockStack;
    std::vector<InlineStyle> styleStack;

    ParserContext() {
        styleStack.push_back(InlineStyle::Plain);
    }

    Block* CurrentBlock() {
        if (blockStack.empty()) return nullptr;
        return blockStack.top();
    }
};

static BlockType MapBlockType(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
        case MD_BLOCK_P: return BlockType::Paragraph;
        case MD_BLOCK_QUOTE: return BlockType::Blockquote;
        case MD_BLOCK_UL: return BlockType::UnorderedList;
        case MD_BLOCK_OL: return BlockType::OrderedList;
        case MD_BLOCK_LI: return BlockType::ListItem;
        case MD_BLOCK_HR: return BlockType::ThematicBreak;
        case MD_BLOCK_CODE: return BlockType::CodeBlock;
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
        default: return InlineStyle::Plain;
    }
}

static int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    
    Block block;
    block.type = MapBlockType(type, detail);

    if (ctx->blockStack.empty()) {
        ctx->doc.blocks.push_back(std::move(block));
        ctx->blockStack.push(&ctx->doc.blocks.back());
    } else {
        auto* parent = ctx->blockStack.top();
        parent->children.push_back(std::move(block));
        ctx->blockStack.push(&parent->children.back());
    }
    return 0;
}

static int LeaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    auto* ctx = static_cast<ParserContext*>(userdata);
    if (!ctx->blockStack.empty()) {
        ctx->blockStack.pop();
    }
    return 0;
}

static int EnterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)detail;
    auto* ctx = static_cast<ParserContext*>(userdata);
    ctx->styleStack.push_back(MapSpanType(type));
    return 0;
}

static int LeaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    auto* ctx = static_cast<ParserContext*>(userdata);
    if (ctx->styleStack.size() > 1) {
        ctx->styleStack.pop_back();
    }
    return 0;
}

static int TextCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    (void)type;
    auto* ctx = static_cast<ParserContext*>(userdata);
    auto* currentBlock = ctx->CurrentBlock();
    if (!currentBlock) return 0;

    std::string str(text, size);
    InlineStyle currentStyle = ctx->styleStack.back();

    if (!currentBlock->inlineRuns.empty() && currentBlock->inlineRuns.back().style == currentStyle) {
        currentBlock->inlineRuns.back().text += str;
    } else {
        currentBlock->inlineRuns.push_back({currentStyle, str});
    }

    return 0;
}

DocumentModel MarkdownParser::Parse(const std::string& source) {
    ParserContext ctx;
    MD_PARSER parser = {0};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = EnterBlockCallback;
    parser.leave_block = LeaveBlockCallback;
    parser.enter_span = EnterSpanCallback;
    parser.leave_span = LeaveSpanCallback;
    parser.text = TextCallback;

    md_parse(source.c_str(), static_cast<MD_SIZE>(source.size()), &parser, &ctx);

    return std::move(ctx.doc);
}

} // namespace mdviewer
