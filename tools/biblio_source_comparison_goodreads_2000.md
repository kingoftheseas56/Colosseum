# Biblio Metadata Source Comparison

## Inputs

- LibGen rows: 2000
- OceanofPDF rows: 2000
- Common title+author keys: 2000

## Headline

OceanofPDF is much stronger for series/order assertions. LibGen is stronger for ISBN and direct file-hash/download assertions. Neither is safe as canonical truth by itself.

## Metrics

| Metric | LibGen | OceanofPDF | Winner |
|---|---:|---:|---|
| Search hit | 1315/2000 (65.8%) | 1840/2000 (92.0%) | OceanofPDF |
| Exact title | 924/2000 (46.2%) | 1440/2000 (72.0%) | OceanofPDF |
| Exact author | 1224/2000 (61.2%) | 1318/2000 (65.9%) | OceanofPDF |
| Series present | 763/2000 (38.1%) | 1276/2000 (63.8%) | OceanofPDF |
| Expected series correct | 566/2000 (28.3%) | 1174/2000 (58.7%) | OceanofPDF |
| Expected position correct | 601/2000 (30.0%) | 1180/2000 (59.0%) | OceanofPDF |
| ISBN present | 1035/2000 (51.7%) | 529/2000 (26.5%) | LibGen |
| Publication year/date present | 1211/2000 (60.6%) | 587/2000 (29.3%) | LibGen |
| Download/format candidate visible | 1315/2000 (65.8%) | 587/2000 (29.3%) | LibGen |

## Agreement Matrix: Expected Series

- Both correct: 344/2000 (17.2%)
- Ocean only correct: 830/2000 (41.5%)
- LibGen only correct: 222/2000 (11.1%)
- Neither correct: 604/2000 (30.2%)

## Suggested Source Weights

| Claim Type | Goodreads seed | OceanofPDF | LibGen |
|---|---:|---:|---:|
| canonical work title/author seed | 0.90 | 0.45 | 0.45 |
| series membership | 0.85 | 0.78 | 0.45 |
| series ordinal | 0.85 | 0.80 | 0.48 |
| ISBN/edition identity | 0.65 | 0.35 | 0.78 |
| downloadable edition candidate | 0.00 | 0.40 | 0.85 |

## Sample Source Disagreements

- 'Tis A Memoir / Frank McCourt: expected Frank McCourt #2; LibGen='', Ocean='Frank McCourt, #2'
- 13 Little Blue Envelopes / Maureen Johnson: expected Little Blue Envelope #1; LibGen='Little Blue Envelope Book 1', Ocean='None'
- 2010: Odyssey Two / Arthur C. Clarke: expected Space Odyssey #2; LibGen='', Ocean='Space Odyssey, #2'
- 3rd Degree / James Patterson: expected Women's Murder Club #3; LibGen='Womens Murder Club 3', Ocean='Women’s Murder Club, #3'
- 4th of July / James Patterson: expected Women's Murder Club #4; LibGen='', Ocean='Women’s Murder Club, #4'
- A Bear Called Paddington / Michael Bond: expected Paddington Bear #1; LibGen='', Ocean='Paddington, #1'
- A Beautiful Wedding / Jamie McGuire: expected Beautiful #2.5; LibGen='', Ocean='Beautiful, #2.5'
- A Breath of Snow and Ashes / Diana Gabaldon: expected Outlander #6; LibGen='', Ocean='Outlander, #6'
- A Caress of Twilight / Laurell K. Hamilton: expected Merry Gentry #2; LibGen='', Ocean='Merry Gentry, #2'
- A Child Called "It" / Dave Pelzer: expected Dave Pelzer #1; LibGen='', Ocean='Dave Pelzer, #1'
- A Court of Thorns and Roses / Sarah J. Maas: expected A Court of Thorns and Roses #1; LibGen='A Court of Thorns and Roses 3', Ocean=''
- A Court of Wings and Ruin / Sarah J. Maas: expected A Court of Thorns and Roses #3; LibGen='A Court of Thorns and Roses 3', Ocean=''
- A Crown of Swords / Robert Jordan: expected The Wheel of Time #7; LibGen='Wheel of Time #1', Ocean='Wheel of Time, #7'
- A Darker Shade of Magic / V.E. Schwab: expected Shades of Magic #1; LibGen='', Ocean='Shades of Magic, #1'
- A Darkness At Sethanon / Raymond E. Feist: expected The Riftwar Saga #4; LibGen='Riftwar Saga 4', Ocean='The Riftwar Saga, #4'
- A Discovery of Witches / Deborah Harkness: expected All Souls Trilogy #1; LibGen='', Ocean='All Souls Trilogy, #1'
- A Drink Before the War / Dennis Lehane: expected Kenzie & Gennaro #1; LibGen='', Ocean='Kenzie and Gennaro, #1'
- A Fatal Grace / Louise Penny: expected Chief Inspector Armand Gamache #2; LibGen='', Ocean='Chief Inspector Armand Gamache, #2'
- A Feast for Crows / George R.R. Martin: expected A Song of Ice and Fire #4; LibGen='A Song of Ice and Fire'', 04', Ocean='None'
- A Game of You / Neil Gaiman: expected The Sandman #5; LibGen='Gaiman, Neil - Sandman 32', Ocean='The Sandman, #5'
- A Gathering of Shadows / V.E. Schwab: expected Shades of Magic #2; LibGen='', Ocean='Shades of Magic, #2'
- A Great and Terrible Beauty / Libba Bray: expected Gemma Doyle #1; LibGen='Gemma Doyle volume 1', Ocean='None'
- A Hat Full of Sky / Terry Pratchett: expected Discworld #32; LibGen='', Ocean='Discworld, #32; Tiffany Aching, #2'
- A Kiss of Shadows / Laurell K. Hamilton: expected Merry Gentry #1; LibGen='Meredith Gentry 1', Ocean='Merry Gentry, #1'
- A Living Nightmare / Darren Shan: expected Cirque Du Freak #1; LibGen='Cirque Du Freak: The Saga of Darren Shan 1', Ocean=''
- A Long Way from Chicago / Richard Peck: expected A Long Way from Chicago #1; LibGen='', Ocean='A Long Way from Chicago, #1'
- A Memory of Light / Robert Jordan: expected The Wheel of Time #14; LibGen='', Ocean='The Wheel of Time, #14'
- A Million Suns / Beth Revis: expected Across the Universe #2; LibGen='', Ocean='Across the Universe, #2'
- A Murder Is Announced / Agatha Christie: expected Miss Marple #5; LibGen='', Ocean='Miss Marple, #5'
- A Quick Bite / Lynsay Sands: expected Argeneau #1; LibGen='', Ocean='Argeneau #1'

## Error Buckets

- LibGen errors: 0
- OceanofPDF errors: 3
- OceanofPDF: 1 x `page.goto: Timeout 45000ms exceeded.
Call log:
[2m  - navigating to "https://oceanofpdf.com/page/3/?s=Left%20Behind", w`
- OceanofPDF: 1 x `page.goto: Timeout 45000ms exceeded.
Call log:
[2m  - navigating to "https://oceanofpdf.com/authors/jamie-mcguire/pdf-e`
- OceanofPDF: 1 x `page.goto: Timeout 45000ms exceeded.
Call log:
[2m  - navigating to "https://oceanofpdf.com/page/3/?s=The%20Bet", waiti`
