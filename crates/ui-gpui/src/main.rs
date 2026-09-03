//! GPUI shell for the Colosseum POC.
//!
//! Phase A UI: an app shell with a left nav rail (Home/Continue, Search,
//! Settings) and three real views — Home (rails from `GET /catalog/home`),
//! Detail (hero from `GET /catalog/series/{id}`), and Player (the existing
//! RenderImage frame pump over the native player backend, driven by a 10 ms
//! timer). The pre-Phase-A single-pane catalog list remains reachable behind
//! `COLOSSEUM_UI_LEGACY=1`.
//!
//! Dev hooks (for the text-only gate, no human click required):
//! - `COLOSSEUM_UI_START_VIEW=detail:<id>` opens the detail view directly on
//!   launch (e.g. `detail:7` for "Starlight Academy").
//! - `COLOSSEUM_UI_AUTOPLAY=1` auto-hits Play: on the detail start view it
//!   plays the moment the detail arrives; on the home start view it plays the
//!   top continue-watching title (falling back to trending).

mod gpui_tokio;
mod http;

use std::sync::Arc;

use gpui::prelude::*;
use gpui::{
    div, img, linear_color_stop, linear_gradient, px, size, AnyElement, Application, Context,
    FontWeight, Hsla, InteractiveElement, IntoElement, ObjectFit, ParentElement, Render,
    SharedString, Styled, TitlebarOptions, Window, WindowBounds, WindowOptions,
};
use player::{Player, VideoFrame};
use ui_widgets::{poster_card, rail, theme};

const DAEMON_URL_DEFAULT: &str = "http://127.0.0.1:8123";
const TEST_CLIP: &str = "file:///tmp/colosseum-ui-test.mp4";

/// Which surface the shell is showing. Home/Detail/Player are the Phase A
/// spine; Search and Settings are placeholders parked on the nav rail.
#[derive(Clone, Copy)]
enum AppView {
    Home,
    Detail { series_id: i64 },
    Player,
    Search,
    Settings,
}

/// Nav-rail destinations. `Home` and `Continue` both land on the Home rails
/// (the Continue Watching rail is already the first shelf).
#[derive(Clone, Copy, PartialEq, Eq)]
enum NavItem {
    Home,
    Continue,
    Search,
    Settings,
}

impl NavItem {
    fn label(self) -> &'static str {
        match self {
            NavItem::Home => "Home",
            NavItem::Continue => "Continue",
            NavItem::Search => "Search",
            NavItem::Settings => "Settings",
        }
    }
}

struct CatalogApp {
    legacy: bool,
    autoplay: bool,
    daemon_url: String,
    start_view: Option<String>,

    view: AppView,
    nav: NavItem,
    home: Option<http::Home>,
    detail: Option<http::SeriesDetail>,
    playing_title: Option<String>,

    status: SharedString,

    // Legacy single-pane state (COLOSSEUM_UI_LEGACY=1).
    series: Vec<http::Series>,
    selected: Option<usize>,

    // Shared player state.
    player: Option<Box<dyn Player>>,
    frame: Option<Arc<gpui::RenderImage>>,
}

impl CatalogApp {
    fn new(daemon_url: String, legacy: bool, start_view: Option<String>, autoplay: bool) -> Self {
        Self {
            legacy,
            autoplay,
            daemon_url,
            start_view,
            view: AppView::Home,
            nav: NavItem::Home,
            home: None,
            detail: None,
            playing_title: None,
            status: "connecting to daemon…".into(),
            series: Vec::new(),
            selected: None,
            player: None,
            frame: None,
        }
    }

    /// Kick off the first fetch for whichever mode/env hook was requested.
    fn spawn_initial(&mut self, cx: &mut Context<Self>) {
        if self.legacy {
            self.fetch_catalog_legacy(cx);
            return;
        }
        let detail_id = self
            .start_view
            .as_deref()
            .and_then(|s| s.strip_prefix("detail:"))
            .and_then(|s| s.trim().parse::<i64>().ok());
        if let Some(id) = detail_id {
            self.open_detail(id, cx);
        } else {
            self.fetch_home(cx);
        }
    }

    fn fetch_home(&mut self, cx: &mut Context<Self>) {
        let base = self.daemon_url.clone();
        cx.spawn(async move |this, cx| {
            let fetched = gpui_tokio::Tokio::spawn(cx, async move { http::home(&base).await })
                .await
                .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                match fetched {
                    Ok(home) => {
                        let continue_n = home.continue_watching.len();
                        let trending_n = home.trending.len();
                        state.home = Some(home);
                        state.status =
                            format!("{continue_n} continue / {trending_n} trending").into();
                        if state.autoplay {
                            // Autoplay the top continue-watching title (falling
                            // back to trending) so the player gate is reachable
                            // without a human click.
                            let pick = state
                                .home
                                .as_ref()
                                .and_then(|h| h.continue_watching.first().or(h.trending.first()))
                                .map(|s| (s.id, s.title.clone()));
                            if let Some((_id, title)) = pick {
                                state.enter_player(&title, cx);
                            }
                        }
                    }
                    Err(e) => state.status = SharedString::from(e),
                }
                cx.notify();
            });
        })
        .detach();
    }

    fn fetch_detail(&mut self, id: i64, cx: &mut Context<Self>) {
        let base = self.daemon_url.clone();
        cx.spawn(async move |this, cx| {
            let fetched =
                gpui_tokio::Tokio::spawn(cx, async move { http::series(&base, id).await })
                    .await
                    .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                match fetched {
                    Ok(detail) => {
                        let detail_id = detail.id;
                        state.detail = Some(detail);
                        state.status = format!("loaded series {detail_id}").into();
                        if state.autoplay {
                            let title = state
                                .detail
                                .as_ref()
                                .map(|d| d.title.clone())
                                .unwrap_or_else(|| format!("series {detail_id}"));
                            state.enter_player(&title, cx);
                        }
                    }
                    Err(e) => state.status = SharedString::from(e),
                }
                cx.notify();
            });
        })
        .detach();
    }

    /// Legacy `GET /catalog/search?q=` fetch for the pre-Phase-A single pane.
    fn fetch_catalog_legacy(&mut self, cx: &mut Context<Self>) {
        let base = self.daemon_url.clone();
        cx.spawn(async move |this, cx| {
            let fetched =
                gpui_tokio::Tokio::spawn(cx, async move { http::search(&base, "").await })
                    .await
                    .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                match fetched {
                    Ok(series) => {
                        state.status = format!("{} series from daemon", series.len()).into();
                        state.series = series;
                        // autoplay on launch keeps the legacy spike testable
                        // without a human click.
                        if !state.series.is_empty() {
                            state.selected = Some(0);
                            let title = state.series[0].title.clone();
                            state.start_playback(cx, &title, TEST_CLIP);
                        }
                    }
                    Err(e) => state.status = SharedString::from(e),
                }
                cx.notify();
            });
        })
        .detach();
    }

    fn open_detail(&mut self, id: i64, cx: &mut Context<Self>) {
        self.view = AppView::Detail { series_id: id };
        self.detail = None;
        self.status = format!("loading series {id}…").into();
        self.stop_playback();
        self.fetch_detail(id, cx);
        cx.notify();
    }

    fn enter_player(&mut self, title: &str, cx: &mut Context<Self>) {
        self.playing_title = Some(title.to_string());
        self.view = AppView::Player;
        self.start_playback(cx, title, TEST_CLIP);
        cx.notify();
    }

    fn navigate(&mut self, item: NavItem, cx: &mut Context<Self>) {
        self.nav = item;
        self.view = match item {
            NavItem::Home | NavItem::Continue => AppView::Home,
            NavItem::Search => AppView::Search,
            NavItem::Settings => AppView::Settings,
        };
        self.stop_playback();
        cx.notify();
    }

    /// Back from the player: return to the detail we came from, or Home.
    fn go_back(&mut self, cx: &mut Context<Self>) {
        self.stop_playback();
        let detail_id = self.detail.as_ref().map(|d| d.id);
        self.view = match detail_id {
            Some(id) => AppView::Detail { series_id: id },
            None => AppView::Home,
        };
        cx.notify();
    }

    fn go_home(&mut self, cx: &mut Context<Self>) {
        self.nav = NavItem::Home;
        self.view = AppView::Home;
        self.stop_playback();
        cx.notify();
    }

    fn nav_item_active(&self, item: NavItem) -> bool {
        match item {
            NavItem::Home | NavItem::Continue => matches!(self.view, AppView::Home),
            NavItem::Search => matches!(self.view, AppView::Search),
            NavItem::Settings => matches!(self.view, AppView::Settings),
        }
    }

    fn start_playback(&mut self, cx: &mut Context<Self>, label: &str, url: &str) {
        match player::native() {
            Ok(mut player) => {
                if let Err(e) = player.load(url) {
                    self.status = format!("{label}: load failed: {e}").into();
                    cx.notify();
                    return;
                }
                player.play();
                let backend = player::backend_name();
                self.status = format!("{backend}: playing {label}").into();
                self.player = Some(player);
                self.start_pump(cx);
            }
            Err(e) => self.status = format!("no native player: {e}").into(),
        }
    }

    fn stop_playback(&mut self) {
        self.player = None;
        self.frame = None;
        self.playing_title = None;
    }

    /// Timer-driven frame pump: polls the player on its own ~10 ms schedule so
    /// a decode hiccup (a `None` poll) can never end the repaint chain. Runs
    /// until the entity is dropped or playback ends.
    fn start_pump(&mut self, cx: &mut Context<Self>) {
        cx.spawn(async move |this, cx| loop {
            let alive = this
                .update(cx, |app: &mut CatalogApp, cx| {
                    app.pump(cx);
                    app.player.is_some()
                })
                .unwrap_or(false);
            if !alive {
                break;
            }
            cx.background_executor()
                .timer(std::time::Duration::from_millis(10))
                .await;
        })
        .detach();
    }

    /// Poll the player for a fresh frame and repaint if one arrived.
    fn pump(&mut self, cx: &mut Context<Self>) {
        let Some(p) = self.player.as_mut() else {
            return;
        };
        if let Some(f) = p.next_frame() {
            if let Some(rimg) = bgra_to_render_image(f) {
                self.frame = Some(rimg);
                cx.notify();
            }
        }
        if let Some(player::PlayerEvent::Ended) = p.event() {
            self.status = "ended".into();
            cx.notify();
        }
    }
}

impl Render for CatalogApp {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        if self.legacy {
            self.render_legacy(cx)
        } else {
            self.render_shell(cx)
        }
    }
}

impl CatalogApp {
    fn render_shell(&self, cx: &mut Context<Self>) -> AnyElement {
        let content = match self.view {
            AppView::Home => self.render_home(cx),
            AppView::Detail { series_id } => self.render_detail(cx, series_id),
            AppView::Player => self.render_player(cx),
            AppView::Search => self.render_placeholder("Search"),
            AppView::Settings => self.render_placeholder("Settings"),
        };
        div()
            .size_full()
            .flex()
            .flex_row()
            .bg(theme::colors::STAGE)
            .text_color(theme::colors::INK)
            .font_family(".SystemUIFont")
            .child(self.render_nav_rail(cx))
            .child(
                div()
                    .id("shell-content")
                    .flex_1()
                    .h_full()
                    .overflow_y_scroll()
                    .child(content),
            )
            .into_any_element()
    }

    fn render_nav_rail(&self, cx: &mut Context<Self>) -> AnyElement {
        let items = [
            NavItem::Home,
            NavItem::Continue,
            NavItem::Search,
            NavItem::Settings,
        ];
        div()
            .flex()
            .flex_col()
            .w(px(220.0))
            .h_full()
            .p_4()
            .gap_2()
            .border_r_1()
            .border_color(theme::colors::EDGE)
            .bg(theme::colors::STAGE_DEEP)
            .child(
                div()
                    .text_size(px(20.0))
                    .font_weight(FontWeight::BOLD)
                    .px_2()
                    .py_2()
                    .child("Colosseum"),
            )
            .children(items.iter().map(|item| {
                let item = *item;
                let active = self.nav_item_active(item);
                let label = item.label();
                div()
                    .id(label)
                    .py_2()
                    .px_3()
                    .rounded_md()
                    .cursor_pointer()
                    .text_size(px(14.0))
                    .bg(if active {
                        theme::colors::GOLD
                    } else {
                        gpui::transparent_black()
                    })
                    .text_color(if active {
                        theme::colors::ON_GOLD
                    } else {
                        theme::colors::INK_DIM
                    })
                    .hover(|style| {
                        style.bg(if active {
                            shade(theme::colors::GOLD, 0.06)
                        } else {
                            gpui::hsla(0.6, 0.2, 0.14, 1.0)
                        })
                    })
                    .on_click(cx.listener(move |state, _ev, _window, cx| {
                        state.navigate(item, cx);
                    }))
                    .child(label)
            }))
            .child(div().flex_1())
            .child(
                div()
                    .px_2()
                    .text_size(px(11.0))
                    .text_color(theme::colors::INK_DIMMER)
                    .child(self.status.clone()),
            )
            .into_any_element()
    }

    fn render_home(&self, cx: &mut Context<Self>) -> AnyElement {
        let Some(home) = &self.home else {
            return self.render_placeholder("Loading home…");
        };
        let continue_items: Vec<AnyElement> = home
            .continue_watching
            .iter()
            .map(|s| clickable_poster(s, cx))
            .collect();
        let trending_items: Vec<AnyElement> = home
            .trending
            .iter()
            .map(|s| clickable_poster(s, cx))
            .collect();
        div()
            .flex()
            .flex_col()
            .p_6()
            .gap_y(theme::spacing::SHELF_GAP)
            .child(rail("Continue Watching", continue_items))
            .child(rail("Trending", trending_items))
            .into_any_element()
    }

    fn render_detail(&self, cx: &mut Context<Self>, series_id: i64) -> AnyElement {
        let Some(detail) = &self.detail else {
            return self.render_placeholder(&format!("Loading series {series_id}…"));
        };
        let id = detail.id;
        let title = detail.title.clone();
        let description = detail.description.clone();
        let color = hex_to_hsla(&detail.poster_color);
        let hero = linear_gradient(
            180.0,
            linear_color_stop(shade(color, 0.06), 0.0),
            linear_color_stop(shade(color, -0.32), 1.0),
        );
        div()
            .flex()
            .flex_col()
            .gap_4()
            .p_6()
            .child(
                div()
                    .id("detail-back")
                    .cursor_pointer()
                    .py_1()
                    .px_3()
                    .rounded_md()
                    .text_color(theme::colors::INK_DIM)
                    .hover(|style| style.text_color(theme::colors::INK))
                    .on_click(cx.listener(|state, _ev, _window, cx| state.go_home(cx)))
                    .child("‹ Back to Home"),
            )
            .child(
                div()
                    .w_full()
                    .rounded(theme::radius::PANEL)
                    .bg(hero)
                    .p_6()
                    .flex()
                    .flex_col()
                    .gap_3()
                    .child(
                        div()
                            .text_size(px(30.0))
                            .font_weight(FontWeight::BOLD)
                            .text_color(theme::colors::INK)
                            .child(SharedString::from(title)),
                    )
                    .child(
                        div()
                            .text_size(px(15.0))
                            .text_color(theme::colors::INK)
                            .child(SharedString::from(description)),
                    )
                    .child(self.play_button(cx, id)),
            )
            .into_any_element()
    }

    fn play_button(&self, cx: &mut Context<Self>, _id: i64) -> AnyElement {
        div()
            .id("play")
            .cursor_pointer()
            .px_4()
            .py_2()
            .rounded(theme::radius::CARD)
            .bg(theme::colors::GOLD)
            .text_color(theme::colors::ON_GOLD)
            .font_weight(FontWeight::BOLD)
            .text_size(px(16.0))
            .hover(|style| style.bg(shade(theme::colors::GOLD, 0.08)))
            .on_click(cx.listener(|state, _ev, _window, cx| {
                let title = state
                    .detail
                    .as_ref()
                    .map(|d| d.title.clone())
                    .unwrap_or_else(|| "series".to_string());
                state.enter_player(&title, cx);
            }))
            .child("▶ Play")
            .into_any_element()
    }

    fn render_player(&self, cx: &mut Context<Self>) -> AnyElement {
        let title = self
            .playing_title
            .clone()
            .unwrap_or_else(|| "Now playing".to_string());
        let video = match &self.frame {
            Some(rimg) => img(rimg.clone())
                .object_fit(ObjectFit::Contain)
                .size_full()
                .into_any_element(),
            None => div()
                .flex_1()
                .h_full()
                .flex()
                .items_center()
                .justify_center()
                .text_color(theme::colors::INK_DIM)
                .child("loading video…")
                .into_any_element(),
        };
        div()
            .size_full()
            .flex()
            .flex_col()
            .child(
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap_3()
                    .p_4()
                    .child(
                        div()
                            .id("player-back")
                            .cursor_pointer()
                            .px_3()
                            .py_1()
                            .rounded_md()
                            .bg(theme::colors::GOLD)
                            .text_color(theme::colors::ON_GOLD)
                            .font_weight(FontWeight::BOLD)
                            .hover(|style| style.bg(shade(theme::colors::GOLD, 0.08)))
                            .on_click(cx.listener(|state, _ev, _window, cx| state.go_back(cx)))
                            .child("‹ Back"),
                    )
                    .child(
                        div()
                            .text_size(px(18.0))
                            .font_weight(FontWeight::BOLD)
                            .child(SharedString::from(title)),
                    ),
            )
            .child(
                div()
                    .flex_1()
                    .h_full()
                    .bg(theme::colors::STAGE_DEEP)
                    .child(video),
            )
            .into_any_element()
    }

    fn render_placeholder(&self, label: &str) -> AnyElement {
        let label = SharedString::from(label.to_owned());
        div()
            .flex()
            .flex_col()
            .gap_2()
            .p_6()
            .child(
                div()
                    .text_size(px(20.0))
                    .font_weight(FontWeight::BOLD)
                    .child(label),
            )
            .child(
                div()
                    .text_size(px(13.0))
                    .text_color(theme::colors::INK_DIM)
                    .child("Not implemented in Phase A."),
            )
            .into_any_element()
    }

    /// The pre-Phase-A single-pane view, kept reachable via
    /// `COLOSSEUM_UI_LEGACY=1` until the shell is proven.
    fn render_legacy(&self, cx: &mut Context<Self>) -> AnyElement {
        let root = div()
            .size_full()
            .flex()
            .flex_row()
            .bg(gpui::hsla(0.6, 0.08, 0.08, 1.0))
            .text_color(gpui::hsla(0.12, 0.2, 0.92, 1.0))
            .font_family(".SystemUIFont")
            .child(
                div()
                    .flex()
                    .flex_col()
                    .w(px(320.0))
                    .h_full()
                    .p_4()
                    .gap_2()
                    .border_r_1()
                    .border_color(gpui::hsla(0.6, 0.1, 0.2, 1.0))
                    .child(
                        div()
                            .text_size(px(20.0))
                            .font_weight(FontWeight::BOLD)
                            .child("Colosseum (legacy)"),
                    )
                    .child(
                        div()
                            .text_size(px(12.0))
                            .text_color(gpui::hsla(0.6, 0.1, 0.55, 1.0))
                            .child(self.status.clone()),
                    )
                    .children(self.series.iter().enumerate().map(|(ix, s)| {
                        let selected = self.selected == Some(ix);
                        let title = s.title.clone();
                        div()
                            .id(("series", ix))
                            .py_2()
                            .px_3()
                            .rounded_md()
                            .cursor_pointer()
                            .bg(if selected {
                                gpui::hsla(0.6, 0.3, 0.22, 1.0)
                            } else {
                                gpui::transparent_black()
                            })
                            .hover(|style| style.bg(gpui::hsla(0.6, 0.25, 0.16, 1.0)))
                            .on_click(cx.listener(move |state, _ev, _window, cx| {
                                state.selected = Some(ix);
                                state.frame = None;
                                state.start_playback(cx, &title, TEST_CLIP);
                            }))
                            .child(SharedString::from(s.title.clone()))
                    })),
            );

        let player_pane = match &self.frame {
            Some(rimg) => img(rimg.clone())
                .object_fit(ObjectFit::Contain)
                .size_full()
                .into_any_element(),
            None => div()
                .flex_1()
                .flex()
                .items_center()
                .justify_center()
                .text_color(gpui::hsla(0.6, 0.1, 0.45, 1.0))
                .child("select a series to play the test clip")
                .into_any_element(),
        };

        root.child(
            div().flex_1().h_full().p_2().child(
                div()
                    .size_full()
                    .bg(gpui::hsla(0.6, 0.05, 0.05, 1.0))
                    .child(player_pane),
            ),
        )
        .into_any_element()
    }
}

/// A poster card wrapped in a clickable host that opens its series detail.
fn clickable_poster(s: &http::Series, cx: &mut Context<CatalogApp>) -> AnyElement {
    let id = s.id;
    let title = s.title.clone();
    let color = hex_to_hsla(&s.poster_color);
    div()
        .id(("poster", id as u64))
        .cursor_pointer()
        .on_click(cx.listener(move |state, _ev, _window, cx| {
            state.open_detail(id, cx);
        }))
        .child(poster_card(title, color))
        .into_any_element()
}

/// RenderImage is BGRA on the wire (per gpui's docs), so VideoFrame bytes are
/// wrapped as-is inside an `image::Frame` for texture upload.
fn bgra_to_render_image(frame: VideoFrame) -> Option<Arc<gpui::RenderImage>> {
    let w = frame.width;
    let h = frame.height;
    let buffer = image::RgbaImage::from_raw(w, h, frame.bgra)?;
    let image_frame = image::Frame::new(buffer);
    let mut frames: smallvec::SmallVec<[image::Frame; 1]> = smallvec::SmallVec::new();
    frames.push(image_frame);
    Some(Arc::new(gpui::RenderImage::new(frames)))
}

/// Shift a color's lightness by `delta` (clamped to `[0, 1]`).
fn shade(color: Hsla, delta: f32) -> Hsla {
    Hsla {
        l: (color.l + delta).clamp(0.0, 1.0),
        ..color
    }
}

/// Parse the daemon's `#rrggbb` poster color into an [`Hsla`]. Falls back to a
/// neutral cover tint if the string is not a six-digit hex color.
fn hex_to_hsla(hex: &str) -> Hsla {
    let hex = hex.trim_start_matches('#');
    if hex.len() < 6 {
        return theme::colors::SLIDE_C1;
    }
    let r = u8::from_str_radix(&hex[0..2], 16).unwrap_or(0) as f32 / 255.0;
    let g = u8::from_str_radix(&hex[2..4], 16).unwrap_or(0) as f32 / 255.0;
    let b = u8::from_str_radix(&hex[4..6], 16).unwrap_or(0) as f32 / 255.0;
    rgb_to_hsla(r, g, b)
}

/// Convert sRGB floats (`0..1`) to gpui's HSLA (hue in turns, `0..1`).
fn rgb_to_hsla(r: f32, g: f32, b: f32) -> Hsla {
    let max = r.max(g).max(b);
    let min = r.min(g).min(b);
    let l = (max + min) / 2.0;
    let delta = max - min;
    if delta <= f32::EPSILON {
        return Hsla {
            h: 0.0,
            s: 0.0,
            l,
            a: 1.0,
        };
    }
    let s = delta / (1.0 - (2.0 * l - 1.0).abs());
    let h_deg = if max == r {
        60.0 * ((g - b) / delta)
    } else if max == g {
        60.0 * ((b - r) / delta + 2.0)
    } else {
        60.0 * ((r - g) / delta + 4.0)
    };
    let h = if h_deg < 0.0 { h_deg + 360.0 } else { h_deg } / 360.0;
    Hsla { h, s, l, a: 1.0 }
}

fn main() {
    env_logger::init();
    let daemon_url = std::env::var("DAEMON_URL").unwrap_or_else(|_| DAEMON_URL_DEFAULT.to_string());
    let legacy = std::env::var("COLOSSEUM_UI_LEGACY").is_ok_and(|v| v == "1");
    let autoplay = std::env::var("COLOSSEUM_UI_AUTOPLAY").is_ok_and(|v| v == "1");
    let start_view = std::env::var("COLOSSEUM_UI_START_VIEW").ok();

    Application::new().run(move |cx| {
        gpui_tokio::init(cx);
        let bounds = WindowBounds::Windowed(gpui::Bounds {
            origin: gpui::Point::new(px(120.0), px(120.0)),
            size: size(px(1280.0), px(800.0)),
        });
        cx.open_window(
            WindowOptions {
                window_bounds: Some(bounds),
                titlebar: Some(TitlebarOptions {
                    title: Some("Colosseum".into()),
                    ..Default::default()
                }),
                ..Default::default()
            },
            |_, cx| {
                cx.new(|cx| {
                    let mut app =
                        CatalogApp::new(daemon_url.clone(), legacy, start_view.clone(), autoplay);
                    app.spawn_initial(cx);
                    app
                })
            },
        )
        .unwrap();
        cx.activate(true);
    });
}
