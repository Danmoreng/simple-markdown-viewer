# Markdown Rendering Fixture

Use this file to visually check Markdown rendering behavior while changing the parser, layout, and Skia renderer.

## Inline Text

Plain text should wrap normally and preserve readable spacing. This sentence is intentionally long enough to wrap at narrower window widths so line breaking, selection, and link hit testing can be checked together.

This paragraph contains *emphasis*, **strong text**, `inline code`, [a relative link](README.md), [a heading fragment link](#task-lists), [an external link](https://example.com), and an inline image placeholder if the image cannot be loaded: ![Missing image alt text](missing-image.png).

Strikethrough text should render with a strike line: ~~completed or removed text~~.

Entities should decode, not display literally: &amp; &lt; &gt; &nbsp; &#x2713;.

## Headings

# Heading 1

## Heading 2

### Heading 3

#### Heading 4

##### Heading 5

###### Heading 6

## Lists

- Unordered item one
- Unordered item two with wrapping text that should align under the item text instead of under the bullet marker when it wraps across multiple lines in a narrower viewport.
  - Nested unordered item
  - Another nested unordered item

1. Ordered item one
2. Ordered item two
   1. Nested ordered item
   2. Another nested ordered item

A separate ordered list with a custom start value:

7. This ordered list starts at 7 in Markdown and should render as 7, 8, 9.
8. The visible numbering should preserve the Markdown start value.
9. Keep this item to confirm the sequence.

1) Parenthesis-delimited ordered lists should keep the `)` marker.
2) Keep this item to confirm the marker style.

## Task Lists

- [ ] Unchecked task
- [x] Checked task with lowercase x
- [X] Checked task with uppercase X
- [ ] Task item with wrapping text that should keep the checkbox aligned with the first line while wrapped lines align with the item text.
  - [ ] Nested unchecked task
  - [x] Nested checked task

## Blockquote

> A blockquote should have a visible accent bar and readable inset text.
>
> It can contain multiple paragraphs and inline formatting like **strong text**, *emphasis*, and `inline code`.

## Code

```cpp
// The fenced code block language tag should be visible in the code block chrome.
#include <vector>

int main() {
    std::vector<int> values = {1, 2, 3};
    return values.empty() ? 0 : values[0];
}
```

Indented code block:

    function example() {
        return "indented code";
    }

```javascript
export function greet(name) {
    const message = `Hello, ${name}`;
    return message.toUpperCase();
}
```

```typescript
type User = {
    id: number;
    name: string;
};

const user: User = { id: 1, name: "Ada" };
```

```json
{
  "name": "markdown-viewer",
  "features": ["syntax", "tables", "links"],
  "enabled": true
}
```

```python
def fib(count: int) -> list[int]:
    values = [0, 1]
    while len(values) < count:
        values.append(values[-1] + values[-2])
    return values
```

```bash
#!/usr/bin/env bash
set -euo pipefail
for file in *.md; do
    echo "checking ${file}"
done
```

```rust
fn main() {
    let values = vec![1, 2, 3];
    println!("{:?}", values);
}
```

```go
package main

import "fmt"

func main() {
    fmt.Println("hello")
}
```

```csharp
public static class Program
{
    public static void Main()
    {
        Console.WriteLine("hello");
    }
}
```

## Tables

| Left aligned | Center aligned | Right aligned |
|:-------------|:--------------:|--------------:|
| Alpha        | Beta           | Gamma         |
| Longer cell content should wrap cleanly | Center | 12345 |
| `code`       | **strong**     | [link](README.md) |
| Short | A much wider content-driven column | Narrow |

## Thematic Break

Text above the rule.

---

Text below the rule.

## Raw HTML

Expected future behavior: decide whether raw HTML should be ignored, shown as source, or rendered in a limited way.

<div class="note">
  <strong>Raw HTML block</strong>
</div>

Inline HTML example: <span>inline span text</span>.
