# Native Mermaid diagrams

This fixture exercises the Rust SVG renderer. Check light, sepia and dark themes,
reader zoom, narrow windows, source copying, searching for `Decision`, and PDF
export. Arrowheads must remain visible. The two final examples must show source
and an explanation instead of disappearing.

## Flowchart

```mermaid
flowchart LR
    A[Open Markdown] --> B{Decision}
    B -->|Diagram| C[Native Rust layout]
    B -->|Text| D[Normal Markdown]
    C --> E[Skia SVG rendering]
    D --> E
```

## Sequence

```mermaid
sequenceDiagram
    participant Reader
    participant Viewer
    participant Rust
    Reader->>Viewer: Open document
    Viewer->>Rust: Mermaid source
    Rust-->>Viewer: SVG and dimensions
    Viewer-->>Reader: Draw with Skia
```

## Class diagram

```mermaid
classDiagram
    Document --> Diagram
    Diagram <|-- Mermaid
    class Document {
        +open()
    }
    class Diagram {
        +render()
    }
```

## State diagram

```mermaid
stateDiagram-v2
    [*] --> Loading
    Loading --> Ready
    Loading --> SourceFallback: Invalid input
    Ready --> [*]
    SourceFallback --> [*]
```

## Entity relationship

```mermaid
erDiagram
    DOCUMENT ||--o{ DIAGRAM : contains
    DIAGRAM ||--|| SOURCE : preserves
```

## Pie chart

```mermaid
pie title Rendering paths
    "Markdown" : 75
    "Mermaid" : 20
    "Fallback" : 5
```

## Styling and Unicode

```mermaid
flowchart TD
    A[Grüße aus Köln] --> B[Schönes Diagramm]
    B --> C[日本語]
    style A fill:#d7eddf,stroke:#31764a,color:#123321
```

## Nested diagram

> Diagram inside a blockquote:
>
> ```mermaid
> flowchart LR
>     A[Shared layout] --> B[Windows and Linux]
> ```

## Invalid source fallback

```mermaid
this-is-not-a-diagram
```

## Unsupported configuration fallback

```mermaid
%%{init: {"theme": "dark"}}%%
flowchart LR
    A --> B
```

Ordinary text after the diagrams must remain visible and selectable.
