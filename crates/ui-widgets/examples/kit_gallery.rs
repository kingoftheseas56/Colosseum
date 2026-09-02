//! Renders every `ui-widgets` widget with an OCR-able text label beneath it.
//!
//! The text-only orchestrator verifies the surface by reading the labels, so
//! each widget is labeled exactly once and the gallery is laid out to fit in a
//! single window (two columns) without scrolling. Run with:
//!
//! ```sh
//! cargo run -p ui-widgets --example kit_gallery
//! ```

use gpui::{
    div, prelude::*, px, size, App, Application, Bounds, Context, FontWeight, Hsla, Render, Window,
    WindowBounds, WindowOptions,
};
use ui_widgets::{poster_card, poster_grid, rail, theme};

const TITLES: [&str; 10] = [
    "Neon District",
    "Paper Lanterns",
    "The Silent Harbor",
    "Red Dunes",
    "Glass Meridian",
    "Static Bloom",
    "Ironwood",
    "Amber Static",
    "Cobalt Run",
    "Last Light",
];

/// A spread of distinct tints so each placeholder poster reads as a different
/// title at a glance. Hues are drawn from the QML slide/cover gradient tints
/// plus a few of our own.
const PALETTE: [Hsla; 8] = [
    theme::colors::COVER_C1,
    theme::colors::COVER_C2,
    theme::colors::SLIDE_C1,
    theme::colors::SLIDE_C2,
    Hsla {
        h: 0.05,
        s: 0.50,
        l: 0.40,
        a: 1.0,
    },
    Hsla {
        h: 0.50,
        s: 0.50,
        l: 0.35,
        a: 1.0,
    },
    Hsla {
        h: 0.80,
        s: 0.40,
        l: 0.40,
        a: 1.0,
    },
    Hsla {
        h: 0.30,
        s: 0.45,
        l: 0.35,
        a: 1.0,
    },
];

struct Gallery;

/// Lay one widget out with a big, high-contrast label beneath it for OCR.
fn section<E: IntoElement>(label: &'static str, element: E) -> impl IntoElement {
    div()
        .flex()
        .flex_col()
        .gap_y(px(12.0))
        .child(element)
        .child(
            div()
                .text_size(px(20.0))
                .font_weight(FontWeight::BOLD)
                .text_color(theme::colors::INK)
                .child(label),
        )
}

/// Stack two sections vertically inside one flex column cell.
fn column(first: impl IntoElement, second: impl IntoElement) -> impl IntoElement {
    div()
        .flex_1()
        .min_w(px(420.0))
        .flex()
        .flex_col()
        .gap_y(px(24.0))
        .child(first)
        .child(second)
}

impl Render for Gallery {
    fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        let single = poster_card(TITLES[0], PALETTE[0]);
        let grid_items = TITLES
            .iter()
            .enumerate()
            .take(3)
            .map(|(i, title)| poster_card(*title, PALETTE[i % PALETTE.len()]));
        let featured = TITLES
            .iter()
            .enumerate()
            .take(5)
            .map(|(i, title)| poster_card(*title, PALETTE[i % PALETTE.len()]));
        let continuing = TITLES
            .iter()
            .rev()
            .enumerate()
            .take(5)
            .map(|(i, title)| poster_card(*title, PALETTE[i % PALETTE.len()]));

        div()
            .size_full()
            .id("gallery")
            .overflow_y_scroll()
            .bg(theme::colors::STAGE)
            .text_color(theme::colors::INK)
            .font_family(".SystemUIFont")
            .p_6()
            .flex()
            .flex_col()
            .gap_y(px(24.0))
            .child(
                div()
                    .text_size(px(30.0))
                    .font_weight(FontWeight::BOLD)
                    .child("ui-widgets kit gallery"),
            )
            .child(
                div()
                    .w_full()
                    .flex()
                    .flex_row()
                    .flex_wrap()
                    .gap(px(24.0))
                    .child(column(
                        section("poster_card", single),
                        section("rail", rail("Featured", featured)),
                    ))
                    .child(column(
                        section("poster_grid", poster_grid(grid_items)),
                        section("rail_2", rail("Continue Watching", continuing)),
                    )),
            )
    }
}

fn main() {
    Application::new().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(1280.0), px(800.0)), cx);
        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(bounds)),
                ..Default::default()
            },
            |_, cx| cx.new(|_| Gallery),
        )
        .unwrap();
        cx.activate(true);
    });
}
