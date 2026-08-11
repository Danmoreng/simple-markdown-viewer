# LaTeX math rendering

Inline mathematics flows with ordinary Markdown: Euler's identity is
$e^{i\pi} + 1 = 0$, the Pythagorean theorem is $a^2 + b^2 = c^2$, and a
fraction can remain inline as $\frac{x+1}{x-1}$.

## Display equations

The quadratic formula uses a centered display run:

$$
\frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
$$

Large operators, limits, Greek letters, and scalable delimiters:

$$
\int_{-\infty}^{\infty} e^{-x^2}\,dx = \sqrt{\pi}
$$

$$
\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}
$$

## Matrices and accents

$$
A = \begin{pmatrix}
  a & b \\
  c & d
\end{pmatrix}, \qquad
A^{-1} = \frac{1}{ad-bc}\begin{pmatrix}
  d & -b \\
  -c & a
\end{pmatrix}
$$

Vectors and accents: $\vec{v}$, $\hat{x}$, $\overline{AB}$, and
$\left\lVert \vec{v} \right\rVert$.

## Markdown interaction and fallbacks

- Formatted surroundings remain Markdown: **energy** $E=mc^2$ and
  [linked math $x^2+y^2$](https://en.wikipedia.org/wiki/Equation).
- Dollar signs in code stay literal: `$not_math$`.
- Ordinary prices need no escaping and stay text: $19.99 and $24.99.
- A single unmatched dollar sign stays text: $
- Dollar-wrapped prose stays text under the conservative math policy: $sale$.
- Unsupported or excessively large formulas remain visible as their original
  `$source$` instead of disappearing.
