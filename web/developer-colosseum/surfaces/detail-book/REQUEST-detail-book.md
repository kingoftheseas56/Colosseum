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


## QML deletion blocker

The current Arc 54 branch still has a runtime reference in `qml/Main.qml`:

```qml
source: "BiblioBook.qml"
```

W2-9 is explicitly forbidden from editing `Main.qml`. Deleting `qml/BiblioBook.qml` before that shared owner removes or replaces the loader would break QML loading, even though web-owned Biblio opens can route to `detail.book`.

There are also static/harness references in the test suite that intentionally inspect or instantiate `BiblioBook.qml`. W2-9 will not rewrite those outside its owned files.

Therefore the QML file is not safe to delete from this lane until the shared deletion map / Main owner resolves the loader reference. This is a contract-blocked deletion, not a silent deferral.
