"""Regenerate the THEME block of styles/tokens.css from qml/Theme.qml (CONTRACT §4.2).

Run from anywhere:  python web/developer-colosseum/dev/gen_tokens.py
Only the block between the GENERATED markers is rewritten; the house scale below it is hand-owned by Claude.
"""
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
THEME = HERE.parents[2] / 'qml' / 'Theme.qml'
TOKENS = HERE.parent / 'styles' / 'tokens.css'
BEGIN = '/* GENERATED from qml/Theme.qml by dev/gen_tokens.py — do not edit by hand */'
END = '/* END GENERATED */'

PROP = re.compile(r'readonly\s+property\s+(color|string|int|real)\s+(\w+)\s*:\s*(.+?)\s*(?://.*)?$')
RGBA = re.compile(r'Qt\.rgba\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*\)')


def css_value(kind, raw):
    raw = raw.strip()
    m = RGBA.match(raw)
    if m:
        r, g, b, a = (float(x) for x in m.groups())
        return f'rgba({round(r * 255)},{round(g * 255)},{round(b * 255)},{a:g})'
    if raw.startswith('"') and raw.endswith('"'):
        val = raw[1:-1]
        return f'"{val}"' if kind == 'string' else val
    if kind in ('int', 'real'):
        return f'{raw}px'
    return raw


def main():
    lines = []
    for line in THEME.read_text(encoding='utf-8').splitlines():
        m = PROP.search(line)
        if m:
            kind, name, raw = m.groups()
            lines.append(f'  --{name}: {css_value(kind, raw)};')
    block = BEGIN + '\n:root{\n' + '\n'.join(lines) + '\n}\n' + END
    text = TOKENS.read_text(encoding='utf-8') if TOKENS.exists() else BEGIN + '\n' + END + '\n'
    text = re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END), lambda _: block, text, flags=re.S)
    TOKENS.write_text(text, encoding='utf-8')
    print(f'{len(lines)} theme tokens -> {TOKENS}')


if __name__ == '__main__':
    main()
