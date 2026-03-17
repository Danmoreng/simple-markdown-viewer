#pragma once
#include <string>
#include <vector>
#include <variant>

namespace mdviewer {

enum class BlockType {
    Paragraph,
    Heading1,
    Heading2,
    Heading3,
    Heading4,
    Heading5,
    Heading6,
    UnorderedList,
    OrderedList,
    Blockquote,
    CodeBlock,
    ThematicBreak,
    ListItem
};

enum class InlineStyle {
    Plain,
    Emphasis,
    Strong,
    Code,
    Link
};

struct InlineRun {
    InlineStyle style;
    std::string text;
};

struct Block {
    BlockType type;
    std::vector<InlineRun> inlineRuns;
    std::vector<Block> children; // For nested blocks like lists
};

struct DocumentModel {
    std::vector<Block> blocks;
};

} // namespace mdviewer
