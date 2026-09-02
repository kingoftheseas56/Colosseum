# POC implementation queue

Work items for the ralph loop. Each item is also a pi todo (`.pi/todos`) with
the same id — agents should claim the todo, implement, verify, and close it.
Parallel-safe: each item owns a disjoint file set. Spawn agents with the
deepseek model unless a body says otherwise.

| id | item | owns | blocked by |
|---|---|---|---|
| catalog-core | implement `Catalog` (rusqlite) | crates/catalog/** | — |
| account-core | port account core slice from Go | crates/account/** | — |
| daemon-routes | error mapping + endpoint polish | crates/daemon/** | — |
| smoke-green | full smoke incl. account flow | scripts/smoke.sh | catalog-core, account-core, daemon-routes |
| windows-cross | `cargo zigbuild` x86_64-pc-windows-gnu green | toolchain files only | — |
| docs-decisions | architecture + library decisions doc | docs/rust-poc.md | — |

Definition of done for every item: `mise run lint` and `cargo test --workspace`
green in this tree, un-ignored spec tests passing, no files touched outside the
owned set, commit message references the item id.

Oracles (read before writing):
- account behavior: `server/account-service/internal/` (Go is the source of
  truth for wire shapes, error codes, and domain policy; reconcile the
  assumptions already written in `crates/account/src/lib.rs` doc comments).
- catalog behavior: spec test in `crates/catalog/src/lib.rs`.

Assumptions needing reconciliation during account-core (document outcomes in
docs/rust-poc.md): password policy floor, access-token TTL, username rules,
device-trust acquisition rules.
