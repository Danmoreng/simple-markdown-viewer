---
title: Front Matter Fixture
author: Simple Markdown Viewer
date: 2026-08-09
tags:
  - markdown
  - metadata
description: This uncommon field should stay hidden from the compact metadata bar.
custom_build_flag: true
---

# Front Matter Body

Title, author, date, and tags should appear in one compact metadata bar. The
description, custom field, raw source, and delimiters should stay hidden. At a
narrow window width the bar may wrap cleanly onto additional lines.

## Context Menu Checks

Right-click this local image and verify that the menu offers **Open Image**,
**Copy Image Path**, and **Open Image in File Manager**.

![Local rendering fixture](front-matter-fixture.svg)

Right-click inside this table and verify that both TSV and CSV copy actions are
available.

| Name | Description | Value |
| --- | --- | ---: |
| Alpha | Plain value | 1 |
| Beta | Contains a comma, which must be quoted in CSV | 2 |
| Gamma | Contains a "quoted value" | 3 |
