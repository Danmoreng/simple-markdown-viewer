# Image loading requires an explicit action

Opening this fixture must not contact any of the servers below. Each placeholder
must show the complete address before its Load image button. Clicking elsewhere
in the placeholder, opening the context menu, searching, scrolling, resizing and
printing must not start a request. Only activate the examples on a test server
you control; the `.invalid` hosts intentionally do not provide images.

## HTTPS, long address

![Full URL](https://images.example.invalid/documentation/releases/2026/illustrations/overview-with-a-deliberately-long-filename.png?width=1200&theme=light&version=review-fixture)

## HTTP

![](http://images.example.invalid/diagram.svg)

## Network share

![Network image](//server.example.invalid/shared/documentation/diagrams/architecture.png)

## Unicode source

![Unicode](https://images.example.invalid/日本語/説明図と非常に長いファイル名の例.png?caption=Übersicht)

## Local image remains automatic

![Application icon](../app_icon.png)

## Links in every table column

| First | Second | Third |
| --- | --- | --- |
| [README](../README.md) | [Math](latex-math.md) | [Mermaid](mermaid.md) |
