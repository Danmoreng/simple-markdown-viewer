# Bidirectional and complex-text fixture

This document exercises native complex-text shaping, Unicode BiDi behavior, interaction geometry, and direction-aware block rendering.

## Mixed paragraphs

English before العربية بعد English 123 (v2.0).

שלום world 123 — العربية and English.

السَّلَامُ عَلَيْكُمْ — لَا إِلٰهَ إِلَّا اللهُ — Arabic combining marks and lam-alef sequences.

עברית עם סימני פיסוק (גרסה 2.0), מספרים 123, ו-English באמצע.

Start LTR: English ثم العربية ثم עברית and back to English (42).

بداية RTL: العربية with **strong English**, *emphasized עברית*, ~~نص مشطوب~~, and [English link](https://example.com/path?q=1).

Inline code stays logical inside RTL prose: العربية `const value = input[2];` ثم עברית.

## Nested lists and source-preserving code

- عنصر عربي في قائمة يسارية with English 123
  1. פריט עברי מקונן (nested Hebrew item)
     - mixed العربية / English / עברית

       ```cpp
       #include <string>

       const char* greeting = "مرحبا";
       const char* farewell = "להתראות";
       // הערה בעברית with value = 123
       auto result = parse(input[2]);
       *pointer = value;
       >literal source text remains visible;
       ```

- English parent item
  - عنصر عربي متداخل تحت عنصر إنجليزي
    1. English child under Arabic text

1. פריט עברי ממוספר
   - English nested bullet
     - مهمة عربية في المستوى الثالث

- [ ] משימה בעברית שעדיין לא הושלמה
- [x] مهمة عربية مكتملة
- [ ] English task with العربية and עברית

## Long code lines and horizontal scrolling

```cpp
const std::string mixed_message = "هذا سطر عربي طويل جدا لاختبار التمرير الأفقي مع English tokens, value = 123456789, punctuation (alpha/beta), and Hebrew text שלום עולם without wrapping the source line";
// הערה ארוכה מאוד בעברית עם English identifiers such_as_this_one, numbers 987654321, brackets [index + 2], and Arabic العربية to exercise mixed-direction source rendering.
```

## Quotes and alerts

> اقتباس عربي with English and 42 (v3).
>
> פסקה עברית נוספת עם العربية and a [mixed link](https://example.com/rtl).

> [!NOTE]
> ملاحظة عربية with English metadata and עברית.

> [!WARNING]
> אזהרה בעברית with العربية, English, and 123.

## Direction-aware details

<details>
<summary>English summary with العربية</summary>

Collapsed LTR details body with עברית.

</details>

<details open>
<summary>ملخص عربي with English 123</summary>

محتوى عربي مفتوح with English and עברית.

</details>

## Tables

| العربية | English | עברית |
| ---: | :--- | ---: |
| قيمة 123 | mixed (v2) | ערך 456 |
| نص **قوي** | [link](https://example.com/table) | טקסט *מודגש* |
| العربية مع English | value = `input[2]` | עברית עם 789 |

## Atomic inline content

معادلة عربية بجانب inline math $e^{i\pi}+1=0$ ثم English text.

עברית ליד תמונה מקומית, then English: ![Local SVG fixture](front-matter-fixture.svg)

Display math remains a dedicated centered line:

$$\frac{-b \pm \sqrt{b^2-4ac}}{2a}$$

## Search, selection, and links

Search target العربية appears before English; English appears before יעד חיפוש בעברית; then العربية appears again.

Select across this whole sentence: Start English — العربية الوسطى — עברית באמצע — English end.

Link fragments must remain clickable: العربية [OpenAI](https://openai.com/) עברית.

## مقدمة

عنوان عربي مكرر لاختبار روابط العناوين والمنظور الجانبي.

## مقدمة

نفس العنوان مرة ثانية؛ يجب أن يبقى ترتيب النص المنطقي مستقرا.

## כותרת בעברית

תוכן בעברית מתחת לכותרת עם English 123.

## Neutral and fallback cases

12345 — (42) — ... — no strong directional character should default to LTR.

Ordinary prices remain text: $5.00 and $10.00. Shell variables remain text: $HOME and $PATH.
