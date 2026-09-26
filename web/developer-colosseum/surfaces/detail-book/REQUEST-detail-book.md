# W2-9 request: book-detail local chrome parity

Source: `qml/BiblioBook.qml:657-714`.

The QML book-detail page owns a local 64px glass bar at y=22 with:
- canonical BackAction, Biblio-specific dim-to-white hover
- `Biblio` display title
- Minimize
- Toggle fullscreen
- Close

Arc 54 web architecture currently owns those controls in the shared persistent shell (`core/shell.js` + shared shell CSS). W2-9 is explicitly forbidden from editing `core/`, `styles/`, `index.html`, or `Main.qml`.

## Gap

W2-9 cannot simultaneously:
1. keep the shared shell untouched, and
2. render the old book-local chrome 1:1.

Adding a second top bar inside `surfaces/detail-book/` would duplicate shell controls and violate the port architecture rather than preserve behavior.

## Requested shared decision

Choose one shared-shell policy for detail surfaces:

- **Policy A:** persistent web shell is authoritative. The book body begins at the same y=108 content line, but the book-local QML top bar is intentionally retired.
- **Policy B:** shared shell gains a supported detail-chrome mode that can project the QML-local title/back/system presentation without a surface editing shared code.

Until that shared decision exists, W2-9 keeps the body/action/source behavior 1:1 and records the chrome as a contract-blocked difference. No local workaround is added.
