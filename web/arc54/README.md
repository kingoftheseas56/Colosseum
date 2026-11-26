# Colosseum Web · Arc 54
Fresh standalone browser frontend alongside the native app. No in-app Qt bridge, donor code, framework or install step. No production deployment.

From this directory:
```sh
python3 -m http.server 8932 --bind 127.0.0.1
node --test router.test.mjs
```
Open `http://127.0.0.1:8932/?mode=fixture#/theatre`. Open `#/theatre/title/tt123` directly, refresh, or navigate Home → Theatre and use browser Back. Hash paths allow static hosting to serve modern deep links without a server rewrite. Use `?mode=fixture` explicitly. Without it the shell is hosted-mode and never pretends to be connected. Checkpoint 2 routes are placeholders awaiting content and styling; no native state is changed.
