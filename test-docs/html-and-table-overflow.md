<p align="center">
  <img src="../app_icon.svg" alt="Simple Markdown Viewer logo" width="160">
</p>

<h1 align="center">Native GitHub-style HTML subset</h1>

<p align="center">
  Local images, centered text, headings, links, and explicit line breaks render through the native document model.<br>
  No browser or WebView is involved.
</p>

<p align="center">
  <a href="../LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="license MIT"></a>
  <img src="https://img.shields.io/badge/status-local--only-green" alt="status local-only">
</p>

The remote badge images above are deliberately not downloaded. They should appear as compact labeled placeholders, and the linked badge remains clickable.

## Native inline HTML

Use <kbd>Ctrl</kbd>+<kbd>F</kbd> to search. Water is H<sub>2</sub>O, and a square can be written as x<sup>2</sup>.

<p>These tags also work inside an allowlisted HTML paragraph: <kbd>Enter</kbd>, CO<sub>2</sub>, and 2<sup>10</sup>.</p>

## Native details

<details>
<summary>Collapsed details</summary>

This content starts hidden and should appear after clicking the summary. It contains **formatted Markdown**, a [local link](../README.md), and a list:

- First nested item
- Second nested item

</details>

<details open>
<summary>Initially open details</summary>

This content starts visible because the safe `open` attribute is present.

</details>

## GitHub alerts

> [!NOTE]
> Useful context that readers should notice.

> [!TIP]
> A practical recommendation.

> [!IMPORTANT]
> Essential information required for successful use.

> [!WARNING]
> Something requires attention before proceeding.

> [!CAUTION]
> A potentially harmful consequence deserves special care.

## Wide table

Shrink the window until this table gets its own horizontal scrollbar. Columns should keep a useful width and cell text may wrap within a bounded column.

| Engine | Checkpoint / KV | Prefill tok/s | TTFT | Effective D2H MTP tok/s | ITL | Sampled peak VRAM | Notes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| gem16 | direct FP8/NVFP4 checkpoint with a deliberately descriptive name | 5,866.86 | 2,792.64 ms | **87.66** | **11.408 ms** | 11,746 MiB | reproducible same-machine result |
| reference | another-long-checkpoint-identifier-that-needs-bounded-cell-wrapping | 6,257.31 | 2,618.35 ms | 82.25 | 12.158 ms | 15,764 MiB | comparison row |

## Long token wrapping

This token must remain inside the document viewport even at a very narrow width:

https://example.com/this-is-one-deliberately-unbroken-token-that-must-wrap-at-valid-utf-8-boundaries-instead-of-escaping-the-reader-surface

## Unsupported HTML remains source

The following unsafe fragment must never execute and should remain visibly rendered as source:

<script>alert("never executed")</script>
