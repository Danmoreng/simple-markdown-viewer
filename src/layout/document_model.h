#pragma once
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
    TableCell
};

enum class InlineStyle {
    Plain,
    Emphasis,
    Strong,
    Code,
    Link,
    Image,
    Strikethrough,
    SyntaxComment,
    SyntaxKeyword,
    SyntaxString,
    SyntaxNumber,
    SyntaxFunction,
    SyntaxType,
    SyntaxVariable,
    SyntaxConstant,
    SyntaxOperator,
    SyntaxPunctuation
};

enum class TaskListState {
    None,
    Unchecked,
    Checked
};

struct InlineRun {
    InlineStyle style;
    std::string text;
    std::string url;
};

struct Block {
    BlockType type;
    TextAlign align = TextAlign::Default;
    TaskListState taskListState = TaskListState::None;
    unsigned orderedListStart = 1;
    char orderedListDelimiter = '.';
    std::string codeLanguage;
    std::vector<InlineRun> inlineRuns;
    std::vector<Block> children; // For nested blocks like lists
};

struct DocumentModel {
    std::vector<Block> blocks;
};

} // namespace mdviewer
