//! Content widgets for the Colosseum media shelves.
//!
//! Each function returns a GPUI element (an `impl IntoElement`) and carries no
//! state of its own; screens assemble these instead of re-inventing poster
//! geometry. Layout is static and the only interaction is hover.

use gpui::{div, linear_color_stop, linear_gradient, prelude::*, Hsla, SharedString};

use crate::theme;

/// A rounded, gradient-tinted poster placeholder.
///
/// No art pipeline yet: the poster plane is a two-stop vertical gradient
/// derived from `color` (lightened at the top, darkened at the bottom),
/// matching the QML `CataloguePosterCard` gallery geometry (148×222, 12px
/// radius, 13px two-line title). Hover brightens the resting edge to soft
/// gold, mirroring the `Bookshelf` gallery edge.
pub fn poster_card(title: impl Into<SharedString>, color: impl Into<Hsla>) -> impl IntoElement {
    let title = title.into();
    let color = color.into();
    let top = lighten(color, 0.10);
    let bottom = darken(color, 0.14);

    div()
        .w(theme::spacing::POSTER_WIDTH)
        .flex()
        .flex_col()
        .gap_y(theme::spacing::TITLE_GAP)
        .cursor_pointer()
        .child(
            div()
                .w_full()
                .h(theme::spacing::POSTER_HEIGHT)
                .rounded(theme::radius::POSTER)
                .border_1()
                .border_color(theme::colors::EDGE_REST)
                .bg(linear_gradient(
                    180.0,
                    linear_color_stop(top, 0.0),
                    linear_color_stop(bottom, 1.0),
                ))
                .hover(|style| style.border_color(theme::colors::GOLD_EDGE)),
        )
        .child(
            div()
                .w_full()
                .text_size(theme::spacing::TITLE_PX)
                .font_weight(gpui::FontWeight::SEMIBOLD)
                .text_color(theme::colors::INK)
                .line_clamp(2)
                .child(title),
        )
}

/// A wrapping grid of poster cards — a simple `flex_wrap` row; virtualization
/// is intentionally not required for the POC.
pub fn poster_grid<E: IntoElement>(items: impl IntoIterator<Item = E>) -> impl IntoElement {
    div()
        .flex()
        .flex_row()
        .flex_wrap()
        .gap(theme::spacing::CARD_GAP)
        .children(items)
}

/// A horizontal rail: a section header above a scrollable row of cards.
///
/// The row scrolls horizontally (`overflow_x_scroll`); its element id is
/// derived from `title`, so give each rail on a screen a distinct title.
pub fn rail<E: IntoElement>(
    title: impl Into<SharedString>,
    children: impl IntoIterator<Item = E>,
) -> impl IntoElement {
    let title = title.into();

    div()
        .flex()
        .flex_col()
        .gap_y(theme::spacing::HEADER_GAP)
        .child(
            div()
                .text_size(theme::spacing::RAIL_TITLE_PX)
                .font_weight(gpui::FontWeight::BOLD)
                .text_color(theme::colors::INK)
                .child(title.clone()),
        )
        .child(
            div()
                .flex()
                .flex_row()
                .flex_nowrap()
                .gap_x(theme::spacing::CARD_GAP)
                .pb_2()
                .id(title)
                .overflow_x_scroll()
                .children(children),
        )
}

/// Shift a color's lightness up by `amount` (clamped to `[0, 1]`).
fn lighten(color: Hsla, amount: f32) -> Hsla {
    Hsla {
        l: (color.l + amount).clamp(0.0, 1.0),
        ..color
    }
}

/// Shift a color's lightness down by `amount` (clamped to `[0, 1]`).
fn darken(color: Hsla, amount: f32) -> Hsla {
    Hsla {
        l: (color.l - amount).clamp(0.0, 1.0),
        ..color
    }
}
