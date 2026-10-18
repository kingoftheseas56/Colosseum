# Vinyl world fixtures
Copied byte-for-byte from kingoftheseas56/Brotherhood at ba52e9c986bc0274d4fe657973b496cb105057b8, agents/vinyl-world-prototype-refs/snapshot/catalogue.json and covers/.
63 albums and 63 images; no live service calls were made by this prototype.
Catalogue facts are supplied snapshot claims, not independently verified by Dots. Thriller facts remain unverified.
Year/language/country/genre and people aliases are manually supplied. trend/essential, playable state, home region and listening profiles are invented prototype inputs. No production recommendation or popularity algorithm is qualified.
Artist cards use representative album covers cropped round, not artist portraits.
Catalogue.js is generated from catalogue.json for a static QML import, avoiding runtime file-access permission changes. Only the standalone host and tests import it. Reusable views receive the world data object.
Regenerate with:
python3 -c "from pathlib import Path; p=Path('tests/fixtures/vinyl-world'); (p/'Catalogue.js').write_text('// Generated from catalogue.json for QML fixture imports; no network or file-access relaxation.\n.pragma library\nvar data = '+(p/'catalogue.json').read_text()+';\n')"
Third-party cover art is reference-only under tests/fixtures; no ownership or licence is asserted.
