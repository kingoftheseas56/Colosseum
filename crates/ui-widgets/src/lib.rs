//! `ui-widgets` — the GPUI media widget kit for Colosseum.
//!
//! Screens are composition, not invention: this crate exports a small set of
//! content components (poster cards, poster grids, rails) plus the frozen
//! design tokens they read from. It is pure [`gpui`] — no daemon, no player,
//! no application chrome (that belongs to `phase-a-ui`).
//!
//! Layout is static; the only interaction is hover. No QML animation
//! semantics are ported. Poster art is a gradient-tinted placeholder until the
//! art pipeline exists.
//!
//! # Example
//!
//! ```sh
//! cargo run -p ui-widgets --example kit_gallery
//! ```
//!
//! The example renders every widget with an OCR-able text label beneath it so
//! a text-only harness can verify the surface.

#![deny(missing_docs)]

pub mod theme;
pub mod widgets;

pub use widgets::{poster_card, poster_grid, rail};
