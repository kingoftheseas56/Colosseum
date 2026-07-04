# Biblio Metadata Source Comparison: First 100 Seeds

## Boundary

Z-Library benchmark used visible public search-page metadata only. It did not log in, open detail pages, click download buttons, or fetch files.

## Headline

Z-Library is useful as a title/discovery hit source, but visible public search pages do not expose enough reliable series or ISBN metadata to improve the canonical graph. OceanofPDF remains the strongest tested source for series/order assertions; LibGen remains strongest for ISBN/download evidence.

## Metrics

| Metric | LibGen | OceanofPDF | Z-Library | Winner |
|---|---:|---:|---:|---|
| Search hit | 97/100 (97.0%) | 88/100 (88.0%) | 99/100 (99.0%) | Z-Library |
| Exact title | 77/100 (77.0%) | 71/100 (71.0%) | 95/100 (95.0%) | Z-Library |
| Exact/nearby author | 92/100 (92.0%) | 71/100 (71.0%) | 64/100 (64.0%) | LibGen |
| Series present | 55/100 (55.0%) | 64/100 (64.0%) | 11/100 (11.0%) | OceanofPDF |
| Expected series correct | 39/100 (39.0%) | 60/100 (60.0%) | 11/100 (11.0%) | OceanofPDF |
| Expected position correct | 43/100 (43.0%) | 64/100 (64.0%) | 11/100 (11.0%) | OceanofPDF |
| ISBN present | 83/100 (83.0%) | 26/100 (26.0%) | 0/100 (0.0%) | LibGen |

## Source Role Update

- Keep Goodreads dump as canonical seed for work and series graph.
- Keep OceanofPDF as high-weight series/order assertion source.
- Keep LibGen as high-weight ISBN/download-candidate source.
- Treat Z-Library public search as optional discovery/title corroboration only unless a cleaner metadata surface is found.
- Anna's Archive should be tested from datasets/API-like exports rather than the public search UI, because the live mirrors are noisy, domain-rotating, and partially protected.
