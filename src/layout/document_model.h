#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <variant>

namespace mdviewer {

enum class TextAlign {
    Default,
    Left,
    Center,
    Right
};

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
    ListItem,
    Table,
    TableRow,
    TableHeaderCell,
    TableCell,
    Metadata,
    RawHtml
};

enum class InlineFormatting : uint8_t {
    None = 0,
    Emphasis = 1 << 0,
    Strong = 1 << 1,
    Code = 1 << 2,
    Strikethrough = 1 << 3,
};

constexpr InlineFormatting operator|(InlineFormatting left, InlineFormatting right) {
    return static_cast<InlineFormatting>(
        static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
}

constexpr InlineFormatting& operator|=(InlineFormatting& left, InlineFormatting right) {
    left = left | right;
    return left;
}

constexpr bool HasFormatting(InlineFormatting value, InlineFormatting flag) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

enum class InlineKind {
    Text,
    Image,
    SoftBreak,
    HardBreak,
};

enum class MetadataRunRole {
    None,
    DotSeparator,
    Divider,
    Tag,
};

enum class SyntaxRole {
    None,
    Comment,
    Keyword,
    String,
    Number,
    Function,
    Type,
    Variable,
    Constant,
    Operator,
    Punctuation,
};

enum class TaskListState {
    None,
    Unchecked,
    Checked
};

struct InlineRun {
    InlineFormatting formatting = InlineFormatting::None;
    InlineKind kind = InlineKind::Text;
    MetadataRunRole metadataRole = MetadataRunRole::None;
    SyntaxRole syntaxRole = SyntaxRole::None;
    std::string text;
    std::string imageSource;
    std::string linkTarget;
    float imageRequestedWidth = 0.0f;
    float imageRequestedHeight = 0.0f;
};

enum class AlertKind {
    None,
    Note,
    Tip,
    Important,
    Warning,
    Caution,
};

struct Block {
    BlockType type;
    TextAlign align = TextAlign::Default;
    TaskListState taskListState = TaskListState::None;
    AlertKind alertKind = AlertKind::None;
    unsigned orderedListStart = 1;
    char orderedListDelimiter = '.';
    std::string codeLanguage;
    std::string metadataFormat;
    std::string rawHtml;
    std::vector<InlineRun> inlineRuns;
    std::vector<Block> children; // For nested blocks like lists
};

struct DocumentModel {
    std::vector<Block> blocks;
};

} // namespace mdviewer
