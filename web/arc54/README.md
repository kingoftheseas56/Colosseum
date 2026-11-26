# Colosseum Web · Arc 54
Fresh standalone browser frontend alongside the native app. No in-app Qt bridge, donor code, framework or install step. No production deployment.

From the repository root:
```sh
python3 -m http.server 8932 --bind 127.0.0.1
node --test web/arc54/router.test.mjs
```
Open `http://127.0.0.1:8932/web/arc54/?mode=fixture#/theatre`. Hash paths work on static hosts without a rewrite. Fixture opt-in is explicit. The web assets reference the native assets in the same repository; serve both at their original relative paths.

[Shared component and viewport instructions](components.md).
