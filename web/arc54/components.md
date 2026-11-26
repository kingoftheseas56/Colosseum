# Shared presentation
Open `/web/arc54/?mode=fixture#/components`. Desktop and narrow preview: use `/web/arc54/viewport.html`. The narrow frame is an actual 390 CSS px viewport, not Android device evidence.
 
- Theme source: pinned `qml/Theme.qml`, 54 px desktop gutter, gold #f0c44a for active/focus/progress, ink/dim/muted, 18% glass edge, 10% tint and 14% highlight.
- Exact repo assets: `assets/fonts/Fraunces-{Regular,Italic}.ttf`, `assets/fonts/selawik/Selawik-{Regular,Semibold}.ttf`, `assets/icons/colosseum.svg`, `assets/wallpaper/cold-ripple.jpg`. Desktop prefers its native Segoe UI before the bundled fallback. Narrow layouts use bundled Selawik. No re-encoded assets or donor code.
- Controls: top bar world links, selected/neutral pills, primary/secondary/disabled buttons, panel and inline status. Tab through the examples to see the gold focus ring; disabled Play cannot focus or initiate anything. Selected pills set aria-current. Buttons and pills have 44 px minimum touch height at the phone breakpoint.
- Layout: sticky responsive header, no decorative motion under reduced-motion preference; sample panels stack under 760 px. Shared component approximations are not native pixel parity.
