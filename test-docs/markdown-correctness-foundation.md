# Markdown Correctness Foundation — Before / After Demo

This fixture demonstrates the three correctness fixes that precede additional
Markdown features.

| Case | Before this change | Expected after this change |
| --- | --- | --- |
| Fenced code | Marker-like code could be rewritten, for example `#include` becoming `# include`. | Every byte inside the fence remains unchanged. |
| Nested inline formatting | Only the innermost style survived. | Bold, italic, strike, code, and link semantics can coexist. |
| Soft break | Every source line ending forced a visible new line. | A soft break behaves like normal whitespace; an explicit hard break starts a new rendered line. |

## 1. Fenced code remains literal

The following block must display `#include`, `*pointer`, and `>literal` exactly
as written:

```cpp
#include <vector>

void update(int* pointer) {
    *pointer = 42;
    // >literal and #not-a-heading stay literal too.
}
```

The same regression is covered inside an indented list container:

- Nested code fixture

    ```text
    #include stays unchanged
    *not a list item
    >not a quote
    ```

## 2. Formatting combinations survive nesting

- ***Bold and italic*** must visibly have both styles.
- [**Bold link text**](https://example.com) must remain bold, colored,
  underlined, and clickable.
- ~~**Bold and struck through**~~ must show both bold and strikethrough.
- **Outer bold with *bold italic inside* and bold outside again.**
- ***[Bold italic link](https://example.com/nested)*** combines three independent semantics.

## 3. Soft break versus hard break

### Soft break

At a wide window size, these two source lines should render as one flowing
paragraph with a normal space between them:

SOFT-A ends on one Markdown source line
SOFT-B continues visually on the same rendered line.

Copying or searching the rendered text should see `SOFT-A ends on one Markdown
source line SOFT-B continues` with a space.

### Hard break

The next two source lines contain two trailing spaces after `HARD-A`. They must
remain on separate rendered lines:

HARD-A ends with an explicit hard break.  
HARD-B starts on the next rendered line.

Copying the rendered text preserves a newline between `HARD-A` and `HARD-B`.

## 4. Syntax role remains independent

Syntax colors in this block come from the syntax role and no longer occupy the
same field as emphasis, links, images, or other inline formatting:

```python
def greeting(name: str) -> str:
    return f"Hello, {name}!"
```
