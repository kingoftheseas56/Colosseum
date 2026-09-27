# Universes surface schema

W2-2 owns `page.universeHall` and `detail.universe`. Every page-specific record uses `layout:"custom"` and a namespaced `Section.data.schema`. Media stays in ordinary contract `Item` records, so the surface never reads `Item.ref`.

## `universes.hall`

One section for the Hall of Worlds. `Section.items` contains installed universe Items in native roster order. Data:

- `schema: "universes.hall"`
- `count: number`

Each Item is `world:"Colosseum"`, `kind:"universe"`, with logo in `cover` and banner in `backdrop`.

## `universes.hero`

The detail masthead. Data:

- `schema: "universes.hero"`
- `template: "cosmere" | "dcau" | "galaxy" | "saga" | "era" | "studio" | "generic"`
- `name`, `kicker`, `blurb`, `banner`, `metaline`
- optional `blurbSource`
- optional `primaryLabel`

If `Section.items[0]` exists it is the native-issued primary media destination.

## `universes.nav`

A page-local atlas / hub / era navigator. Data:

- `schema: "universes.nav"`
- `template: string`
- `title`, optional `subtitle`
- `entries: [{ key, label, sublabel?, targetId, art? }]`

`targetId` is a section id already issued by the feed. It is not a native route.

## `universes.group`

A named media shelf or wall. `Section.items` contains openable media Items. Data:

- `schema: "universes.group"`
- `template: string`
- `label`, optional `note`
- `variant: "rail" | "wall" | "portal"`
- optional `ordinal`
- optional `locked: [{ key, title, subtitle?, cover? }]`

`locked` preserves curated works whose current native open contract cannot represent their identity. They are rendered non-interactively rather than receiving a fabricated route.

## Normal contract sections

DCAU Continue uses `layout:"continue"` with ordinary Continue Items. Loading, empty and error states use the normal Section state contract.

## One Piece boundary

`detail.universe` rejects `com.colosseum.universe.onepiece`. One Piece stays native under CONTRACT §12.6. The shared `open` router currently decides the web route before subscribing; W2-2 records that core seam gap in the Arc 54 request file rather than inventing a surface-side routing workaround.

## `universes.starters`

Cosmere's authored three-entry gateway. `Section.items` holds only books that the native Apple lookup resolved.

- `schema: "universes.starters"`
- `title`
- `entries: [{ key, short, label, note, itemKey? }]`

`itemKey` points at an Item already in the same section. An unresolved authored gate stays visible but is not interactive.

## `universes.duality`

The Saga/generic READ / WATCH split. `Section.items` holds up to the two native-issued media destinations.

- `schema: "universes.duality"`
- `template`
- `leftLabel`, `leftSub`, `leftKey?`
- `rightLabel`, `rightSub`, `rightKey?`

Keys refer only to Items in the same section.

## `universes.eras`

The Era template's canon columns. `Section.items` contains every resolved screen Item plus the optional comics door.

- `schema: "universes.eras"`
- `kicker`
- `columns: [{ key, label, itemKeys, pending }]`
- optional `comicKey`

`itemKeys` / `comicKey` refer only to Items in the same section.
