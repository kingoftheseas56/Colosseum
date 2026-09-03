//! Content widgets for the Colosseum media shelves.
//!
//! Each function returns a GPUI element (an `impl IntoElement`) and carries no
//! state of its own; screens assemble these instead of re-inventing poster
//! geometry. Layout is static and the only interaction is hover.

use gpui::{
    div, linear_color_stop, linear_gradient, point, prelude::*, px, BoxShadow, Hsla, SharedString,
};

use crate::theme;

/// A rounded, gradient-tinted poster placeholder.
///
/// No art pipeline yet: the poster plane is a two-stop vertical gradient
/// derived from `color` (lightened at the top, darkened at the bottom),
/// matching the QML `CataloguePosterCard` gallery geometry (148×222, 12px
/// radius, 13px two-line title). Hover brightens both stops, lifts the card
/// with a soft shadow (gpui's styled API has no `scale`, so the shadow is the
/// depth cue), and warms the resting edge to gold — mirroring the gallery
/// hover treatment.
pub fn poster_card(title: impl Into<SharedString>, color: impl Into<Hsla>) -> impl IntoElement {
    let title = title.into();
    let color = color.into();
    let top = lighten(color, 0.12);
    let bottom = darken(color, 0.18);
    let top_hover = lighten(top, 0.06);
    let bottom_hover = lighten(bottom, 0.06);

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
                .hover(|style| {
                    style
                        .bg(linear_gradient(
                            180.0,
                            linear_color_stop(top_hover, 0.0),
                            linear_color_stop(bottom_hover, 1.0),
                        ))
                        .border_color(theme::colors::GOLD_EDGE)
                        .shadow(vec![BoxShadow {
                            color: Hsla {
                                h: 0.0,
                                s: 0.0,
                                l: 0.0,
                                a: 0.45,
                            },
                            offset: point(px(0.0), px(8.0)),
                            blur_radius: px(18.0),
                            spread_radius: px(0.0),
                        }])
                }),
        )
        .child(
            div()
                .w_full()
                .min_h(theme::spacing::TITLE_MIN_HEIGHT)
                .text_size(theme::typography::POSTER_TITLE_PX)
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
/// The header carries a gold accent bar (gold is a SPARING accent) beside the
/// 22px title, echoing `WidgetHeader.qml`. The row scrolls horizontally
/// (`overflow_x_scroll`); its element id is derived from `title`, so give each
/// rail on a screen a distinct title.
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
                .flex()
                .flex_row()
                .items_center()
                .gap_2()
                .child(
                    div()
                        .w(px(4.0))
                        .h(theme::typography::RAIL_TITLE_PX)
                        .rounded_full()
                        .bg(theme::colors::GOLD),
                )
                .child(
                    div()
                        .text_size(theme::typography::RAIL_TITLE_PX)
                        .font_weight(gpui::FontWeight::BOLD)
                        .text_color(theme::colors::INK)
                        .child(title.clone()),
                ),
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
