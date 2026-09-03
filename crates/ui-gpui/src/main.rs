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
//! - `COLOSSEUM_UI_START_VIEW=detail:<id>` opens the seeded series detail
//!   directly on launch (e.g. `detail:7` for "Starlight Academy").
//! - `COLOSSEUM_UI_START_VIEW=imdb:tt1160419` opens the live detail-by-imdb
//!   view directly (a bare `tt` id defaults to `movie`; `imdb:movie:` /
//!   `imdb:series:` prefixes are also accepted).
//! - `COLOSSEUM_UI_START_VIEW=search:<query>` opens the Search view pre-filled
//!   with `<query>` and fetches immediately (e.g. `search:dune`).
//! - `COLOSSEUM_UI_AUTOPLAY=1` auto-hits Play: on the detail start view it
//!   plays the moment the detail arrives; on the home start view it plays the
//!   top continue-watching title (falling back to trending).
//! - `COLOSSEUM_UI_AUTOPLAY_SOURCE=1` auto-spools the top torrent candidate
//!   the moment a detail-by-imdb SourcesSheet arrives (the spool path then
//!   reaches the player without a human click).

mod gpui_tokio;
mod http;

use std::collections::BTreeMap;
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
/// spine; Search and Settings sit on the nav rail. `DetailImdb` is the slice-4
/// live detail-by-imdb surface (Cinemeta meta + Torrentio SourcesSheet).
#[derive(Clone)]
enum AppView {
    Home,
    Detail { series_id: i64 },
    DetailImdb { media_type: String, tt_id: String },
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

    // Search view state (nav-rail Search made real in slice 4).
    search_query: String,
    search_focus: Option<gpui::FocusHandle>,
    search_seq: u64,
    search_results: Vec<http::SearchRow>,

    // Detail-by-imdb state (live Cinemeta meta + Torrentio SourcesSheet).
    imdb_type: String,
    imdb_id: String,
    imdb_meta: Option<http::CinemetaMeta>,
    sources: Option<http::Sources>,
    source_filter: Option<String>,
    sources_status: SharedString,
    autoplay_source: bool,

    // Shared player state.
    player: Option<Box<dyn Player>>,
    frame: Option<Arc<gpui::RenderImage>>,
}

impl CatalogApp {
    fn new(
        daemon_url: String,
        legacy: bool,
        start_view: Option<String>,
        autoplay: bool,
        autoplay_source: bool,
    ) -> Self {
        Self {
            legacy,
            autoplay,
            autoplay_source,
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
            search_query: String::new(),
            search_focus: None,
            search_seq: 0,
            search_results: Vec::new(),
            imdb_type: String::new(),
            imdb_id: String::new(),
            imdb_meta: None,
            sources: None,
            source_filter: None,
            sources_status: SharedString::default(),
            player: None,
            frame: None,
        }
    }

    /// Kick off the first fetch for whichever mode/env hook was requested.
    fn spawn_initial(&mut self, cx: &mut Context<Self>) {
        self.search_focus = Some(cx.focus_handle());
        if self.legacy {
            self.fetch_catalog_legacy(cx);
            return;
        }
        let Some(start) = self.start_view.clone() else {
            self.fetch_home(cx);
            return;
        };
        let start = start.as_str();

        if let Some(id) = start
            .strip_prefix("detail:")
            .and_then(|s| s.trim().parse::<i64>().ok())
        {
            self.open_detail(id, cx);
            return;
        }
        if let Some((media_type, tt_id)) = parse_imdb_hook(start) {
            self.open_detail_imdb(&media_type, &tt_id, cx);
            return;
        }
        if let Some(query) = start.strip_prefix("search:") {
            self.open_search(query, cx);
            return;
        }
        self.fetch_home(cx);
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

    /// Open the Search view, optionally pre-filling a query and fetching right
    /// away (the `search:<query>` dev hook).
    fn open_search(&mut self, query: &str, cx: &mut Context<Self>) {
        self.nav = NavItem::Search;
        self.view = AppView::Search;
        self.stop_playback();
        if !query.is_empty() {
            self.search_query = query.to_string();
            self.fetch_search(query, cx);
        }
        cx.notify();
    }

    /// Open the live detail-by-imdb view for one `tt` id and fetch its
    /// Cinemeta meta.
    fn open_detail_imdb(&mut self, media_type: &str, tt_id: &str, cx: &mut Context<Self>) {
        self.imdb_type = media_type.to_string();
        self.imdb_id = tt_id.to_string();
        self.imdb_meta = None;
        self.sources = None;
        self.source_filter = None;
        self.sources_status = SharedString::default();
        self.view = AppView::DetailImdb {
            media_type: media_type.to_string(),
            tt_id: tt_id.to_string(),
        };
        self.status = format!("loading {tt_id}…").into();
        self.stop_playback();
        self.fetch_meta(cx);
        cx.notify();
    }

    /// `GET /catalog/meta/{type}/{tt_id}` — the live detail hero.
    fn fetch_meta(&mut self, cx: &mut Context<Self>) {
        let base = self.daemon_url.clone();
        let media_type = self.imdb_type.clone();
        let tt_id = self.imdb_id.clone();
        let tt_label = tt_id.clone();
        cx.spawn(async move |this, cx| {
            let fetched = gpui_tokio::Tokio::spawn(cx, async move {
                http::meta(&base, &media_type, &tt_id).await
            })
            .await
            .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                match fetched {
                    Ok(meta) => {
                        let title = meta.title();
                        state.imdb_meta = Some(meta);
                        state.status = format!("loaded {tt_label}: {title}").into();
                        // `AUTOPLAY_SOURCE=1` reaches the spool path without a
                        // click: once the hero arrives, fetch the candidates
                        // (which then auto-spool the top torrent row).
                        if state.autoplay_source {
                            state.fetch_sources(cx);
                        }
                    }
                    Err(e) => state.status = SharedString::from(e),
                }
                cx.notify();
            });
        })
        .detach();
    }

    /// `GET /sources/imdb/{tt}` — ranked Torrentio candidates, then
    /// (optionally) auto-spool the top torrent row via `AUTOPLAY_SOURCE=1`.
    fn fetch_sources(&mut self, cx: &mut Context<Self>) {
        let base = self.daemon_url.clone();
        let tt_id = self.imdb_id.clone();
        self.sources_status = format!("loading sources for {tt_id}…").into();
        cx.notify();
        cx.spawn(async move |this, cx| {
            let fetched =
                gpui_tokio::Tokio::spawn(
                    cx,
                    async move { http::sources_imdb(&base, &tt_id).await },
                )
                .await
                .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                match fetched {
                    Ok(sources) => {
                        let n = sources.candidates.len();
                        state.status = format!("sources loaded: {n} candidates").into();
                        state.sources_status = format!(
                            "{n} candidates · {}",
                            summarize_counts(&sources.counts_by_addon)
                        )
                        .into();
                        state.sources = Some(sources);
                        // Text-only gate: spool the top torrent candidate the
                        // moment the sheet arrives, no click required.
                        if state.autoplay_source {
                            if let Some(cid) = state.first_torrent_candidate() {
                                state.spool_candidate(&cid, cx);
                            } else {
                                state.sources_status =
                                    "no torrent candidates to spool (direct/offline)".into();
                            }
                        }
                    }
                    Err(e) => {
                        let msg = SharedString::from(e);
                        state.sources_status = msg.clone();
                        state.status = msg;
                    }
                }
                cx.notify();
            });
        })
        .detach();
    }

    /// `POST /torrents/spool` for one candidate. Sets an honest status line
    /// through the request; on success the returned `file://` path is handed
    /// to the existing player view. Direct/offline rows never reach here (the
    /// sheet reports them instead) and a real-swarm stall is surfaced as the
    /// daemon's own `stalled`/`…` envelope message.
    fn spool_candidate(&mut self, candidate_id: &str, cx: &mut Context<Self>) {
        let base = self.daemon_url.clone();
        let cid = candidate_id.to_string();
        let status: SharedString = format!("spooling {cid}…").into();
        self.status = status.clone();
        self.sources_status = status;
        cx.notify();
        cx.spawn(async move |this, cx| {
            let fetched =
                gpui_tokio::Tokio::spawn(cx, async move { http::spool(&base, &cid).await })
                    .await
                    .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                match fetched {
                    Ok(resp) => {
                        let path = resp.path.clone();
                        state.status = format!("spool complete: {path}").into();
                        state.sources_status = format!("spool complete: {path}").into();
                        state.enter_player_url(&path, &path, cx);
                    }
                    Err(e) => {
                        let msg = format!("spool failed: {e}");
                        state.status = msg.clone().into();
                        state.sources_status = msg.into();
                    }
                }
                cx.notify();
            });
        })
        .detach();
    }

    /// The top-ranked torrent candidate id (direct rows are skipped — they
    /// carry a `u:` id and never reach the spool route).
    fn first_torrent_candidate(&self) -> Option<String> {
        self.sources
            .as_ref()?
            .candidates
            .iter()
            .find(|c| c.is_torrent())
            .map(|c| c.id.clone())
    }

    /// Debounce-ish search fetch: every keystroke schedules a ~350 ms delayed
    /// fetch, and only the latest query wins (a generation counter invalidates
    /// stale tasks).
    fn search_debounced(&mut self, cx: &mut Context<Self>) {
        let query = self.search_query.clone();
        self.search_seq += 1;
        let seq = self.search_seq;
        cx.spawn(async move |this, cx| {
            cx.background_executor()
                .timer(std::time::Duration::from_millis(350))
                .await;
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                if state.search_seq == seq && state.search_query == query {
                    state.fetch_search(&query, cx);
                }
            });
        })
        .detach();
    }

    /// `GET /catalog/search?q=…` for the Search view (live Cinemeta rows or
    /// the seeded offline rows, both handled by `http::SearchRow`).
    fn fetch_search(&mut self, query: &str, cx: &mut Context<Self>) {
        let base = self.daemon_url.clone();
        let query = query.to_string();
        let query_label = query.clone();
        self.status = format!("searching {query}…").into();
        cx.notify();
        cx.spawn(async move |this, cx| {
            let fetched =
                gpui_tokio::Tokio::spawn(cx, async move { http::search_rows(&base, &query).await })
                    .await
                    .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
            let _ = this.update(cx, |state: &mut CatalogApp, cx| {
                match fetched {
                    Ok(rows) => {
                        let n = rows.len();
                        state.search_results = rows;
                        state.status = format!("{n} results for \"{query_label}\"").into();
                    }
                    Err(e) => state.status = SharedString::from(e),
                }
                cx.notify();
            });
        })
        .detach();
    }

    /// Filter the SourcesSheet to one add-on name, or `None` for All.
    fn cycle_source_filter(&mut self, name: Option<String>) {
        self.source_filter = name;
    }

    fn enter_player(&mut self, title: &str, cx: &mut Context<Self>) {
        self.playing_title = Some(title.to_string());
        self.view = AppView::Player;
        self.start_playback(cx, title, TEST_CLIP);
        cx.notify();
    }

    /// Enter the player with a specific URL (the spool-complete `file://`
    /// path) rather than the demo clip.
    fn enter_player_url(&mut self, title: &str, url: &str, cx: &mut Context<Self>) {
        self.playing_title = Some(title.to_string());
        self.view = AppView::Player;
        self.start_playback(cx, title, url);
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

    /// Back from the player: return to the content we came from (live imdb
    /// detail, seeded series detail, or Home).
    fn go_back(&mut self, cx: &mut Context<Self>) {
        self.stop_playback();
        if !self.imdb_id.is_empty() {
            let media_type = self.imdb_type.clone();
            let tt_id = self.imdb_id.clone();
            self.view = AppView::DetailImdb { media_type, tt_id };
        } else if let Some(d) = &self.detail {
            self.view = AppView::Detail { series_id: d.id };
        } else {
            self.view = AppView::Home;
        }
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
            NavItem::Home | NavItem::Continue => matches!(&self.view, AppView::Home),
            NavItem::Search => matches!(&self.view, AppView::Search),
            NavItem::Settings => matches!(&self.view, AppView::Settings),
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
        let content = match &self.view {
            AppView::Home => self.render_home(cx),
            AppView::Detail { series_id } => self.render_detail(cx, *series_id),
            AppView::DetailImdb { media_type, tt_id } => {
                self.render_detail_imdb(cx, media_type, tt_id)
            }
            AppView::Player => self.render_player(cx),
            AppView::Search => self.render_search(cx),
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

    /// The Search view: a text input that fetches `/catalog/search` on a
    /// ~350 ms debounce, plus one clickable row per result. Live Cinemeta rows
    /// open detail-by-imdb; seeded offline rows open the existing series
    /// detail. The `search:<query>` dev hook pre-fills and fetches so the
    /// text-only gate needs no keyboard.
    fn render_search(&self, cx: &mut Context<Self>) -> AnyElement {
        let input_text = if self.search_query.is_empty() {
            "Type to search…".to_string()
        } else {
            self.search_query.clone()
        };
        let input = div()
            .id("search-input")
            .flex()
            .flex_row()
            .items_center()
            .px_3()
            .py_2()
            .rounded(theme::radius::CARD)
            .border_1()
            .border_color(theme::colors::EDGE)
            .bg(theme::colors::STAGE_DEEP)
            .text_color(theme::colors::INK)
            .text_size(px(15.0))
            .cursor_text()
            .on_key_down(
                cx.listener(|state, event: &gpui::KeyDownEvent, _window, cx| {
                    state.handle_search_key(event, cx);
                }),
            )
            .child(SharedString::from(input_text));
        let input = match &self.search_focus {
            Some(focus) => input.track_focus(focus),
            None => input,
        };

        let body: AnyElement = if self.search_results.is_empty() {
            div()
                .text_size(px(13.0))
                .text_color(theme::colors::INK_DIM)
                .child("Search Cinemeta for live titles (e.g. \"dune\").")
                .into_any_element()
        } else {
            let rows: Vec<AnyElement> = self
                .search_results
                .iter()
                .map(|row| self.search_row(row, cx))
                .collect();
            div()
                .flex()
                .flex_col()
                .gap_2()
                .children(rows)
                .into_any_element()
        };

        div()
            .flex()
            .flex_col()
            .gap_4()
            .p_6()
            .child(
                div()
                    .text_size(px(22.0))
                    .font_weight(FontWeight::BOLD)
                    .text_color(theme::colors::INK)
                    .child("Search"),
            )
            .child(input)
            .child(body)
            .into_any_element()
    }

    /// One clickable search-result row.
    fn search_row(&self, row: &http::SearchRow, cx: &mut Context<Self>) -> AnyElement {
        let title = row.title();
        let subtitle = row.subtitle();
        let target = match row {
            http::SearchRow::Live(meta) => {
                let media_type = meta
                    .media_type
                    .clone()
                    .unwrap_or_else(|| "movie".to_string());
                let tt_id = meta.id.clone().unwrap_or_default();
                SearchTarget::Imdb { media_type, tt_id }
            }
            http::SearchRow::Seeded(series) => SearchTarget::Series(series.id),
        };
        div()
            .id(SharedString::from(format!("search-row-{title}")))
            .flex()
            .flex_row()
            .items_center()
            .gap_3()
            .px_3()
            .py_2()
            .rounded(theme::radius::CARD)
            .bg(theme::colors::GLASS_TINT)
            .cursor_pointer()
            .hover(|style| style.bg(theme::colors::GLASS_HI))
            .on_click(cx.listener(move |state, _ev, _window, cx| match &target {
                SearchTarget::Imdb { media_type, tt_id } => {
                    state.open_detail_imdb(media_type, tt_id, cx);
                }
                SearchTarget::Series(id) => state.open_detail(*id, cx),
            }))
            .child(
                div()
                    .flex()
                    .flex_col()
                    .gap_1()
                    .child(
                        div()
                            .text_size(px(15.0))
                            .font_weight(FontWeight::BOLD)
                            .text_color(theme::colors::INK)
                            .child(SharedString::from(title)),
                    )
                    .child(
                        div()
                            .text_size(px(12.0))
                            .text_color(theme::colors::INK_DIM)
                            .child(SharedString::from(subtitle)),
                    ),
            )
            .into_any_element()
    }

    /// Minimal text-entry handling for the Search input (no IME): printable
    /// keys append, backspace/escape/enter behave as expected, and each edit
    /// re-arms the debounced fetch.
    fn handle_search_key(&mut self, event: &gpui::KeyDownEvent, cx: &mut Context<Self>) {
        let ks = &event.keystroke;
        if ks.modifiers.control
            || ks.modifiers.platform
            || ks.modifiers.alt
            || ks.modifiers.function
        {
            return;
        }
        match ks.key.as_str() {
            "backspace" => {
                self.search_query.pop();
                self.search_debounced(cx);
            }
            "enter" => {
                let query = self.search_query.clone();
                self.fetch_search(&query, cx);
            }
            "escape" => {
                self.search_query.clear();
                self.search_results.clear();
                cx.notify();
            }
            "space" => {
                self.search_query.push(' ');
                self.search_debounced(cx);
            }
            _ => {
                let ch = ks
                    .key_char
                    .clone()
                    .or_else(|| (ks.key.chars().count() == 1).then(|| ks.key.clone()));
                if let Some(ch) = ch.filter(|c| !c.chars().any(char::is_control)) {
                    self.search_query.push_str(&ch);
                    self.search_debounced(cx);
                }
            }
        }
    }

    /// The live detail-by-imdb view: Cinemeta hero + a SourcesSheet for
    /// Torrentio candidates.
    fn render_detail_imdb(
        &self,
        cx: &mut Context<Self>,
        _media_type: &str,
        tt_id: &str,
    ) -> AnyElement {
        let hero = linear_gradient(
            180.0,
            linear_color_stop(shade(theme::colors::COVER_C1, 0.04), 0.0),
            linear_color_stop(shade(theme::colors::COVER_C2, -0.18), 1.0),
        );

        let back = div()
            .id("imdb-back")
            .cursor_pointer()
            .py_1()
            .px_3()
            .rounded_md()
            .text_color(theme::colors::INK_DIM)
            .hover(|style| style.text_color(theme::colors::INK))
            .on_click(cx.listener(|state, _ev, _window, cx| state.go_home(cx)))
            .child("‹ Back to Home");

        let hero_elem: AnyElement = match &self.imdb_meta {
            Some(meta) => {
                let title = meta.title();
                let subtitle = meta.subtitle();
                let description = meta
                    .description
                    .clone()
                    .filter(|d| !d.is_empty())
                    .unwrap_or_else(|| "No description from Cinemeta.".to_string());
                // Genres + runtime give the hero a bit more of the Cinemeta
                // full-meta surface (poster/background are remote URLs, so the
                // hero stays on the gradient fallback).
                let mut details = meta.genres.join(" · ");
                if let Some(runtime) = meta.runtime.as_deref().filter(|r| !r.is_empty()) {
                    if !details.is_empty() {
                        details.push_str(" · ");
                    }
                    details.push_str(runtime);
                }
                let mut hero_panel = div()
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
                            .text_size(px(13.0))
                            .text_color(theme::colors::INK_DIM)
                            .child(SharedString::from(subtitle)),
                    )
                    .child(
                        div()
                            .text_size(px(15.0))
                            .text_color(theme::colors::INK)
                            .child(SharedString::from(description)),
                    );
                if !details.is_empty() {
                    hero_panel = hero_panel.child(
                        div()
                            .text_size(px(12.0))
                            .text_color(theme::colors::INK_DIM)
                            .child(SharedString::from(details)),
                    );
                }
                hero_panel.child(self.sources_button(cx)).into_any_element()
            }
            None => self.render_placeholder(&format!("Loading {tt_id}…")),
        };

        div()
            .flex()
            .flex_col()
            .gap_4()
            .p_6()
            .child(back)
            .child(hero_elem)
            .child(self.render_sources_sheet(cx))
            .into_any_element()
    }

    /// The Play/Sources CTA on a loaded imdb hero: fetches ranked candidates
    /// and opens the sheet.
    fn sources_button(&self, cx: &mut Context<Self>) -> AnyElement {
        div()
            .id("sources")
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
                state.fetch_sources(cx);
            }))
            .child("▶ Sources")
            .into_any_element()
    }

    /// The candidate sheet under the imdb hero. Only appears once a fetch has
    /// been requested (`sources_status` set) or completed (`sources` present).
    fn render_sources_sheet(&self, cx: &mut Context<Self>) -> AnyElement {
        if self.sources_status.is_empty() && self.sources.is_none() {
            return div().into_any_element();
        }

        let mut sheet = div()
            .id("sources-sheet")
            .flex()
            .flex_col()
            .gap_3()
            .rounded(theme::radius::PANEL)
            .border_1()
            .border_color(theme::colors::EDGE)
            .bg(theme::colors::STAGE_DEEP)
            .p_4();

        sheet = sheet.child(
            div()
                .flex()
                .flex_row()
                .items_center()
                .gap_3()
                .child(
                    div()
                        .text_size(px(18.0))
                        .font_weight(FontWeight::BOLD)
                        .text_color(theme::colors::INK)
                        .child("Sources"),
                )
                .child(
                    div()
                        .text_size(px(12.0))
                        .text_color(theme::colors::INK_DIM)
                        .child(self.sources_status.clone()),
                ),
        );

        let Some(sources) = &self.sources else {
            return sheet.into_any_element();
        };

        // Filter pills follow the install (priority) order from the registry,
        // restricted to the addons that actually contributed candidates and
        // de-duplicated (the live registry can list Torrentio twice).
        let mut addon_names: Vec<String> = Vec::new();
        for addon in &sources.installed_addons {
            if sources.counts_by_addon.contains_key(&addon.name)
                && !addon_names.contains(&addon.name)
            {
                addon_names.push(addon.name.clone());
            }
        }
        for name in sources.counts_by_addon.keys() {
            if !addon_names.contains(name) {
                addon_names.push(name.clone());
            }
        }
        let mut pills: Vec<AnyElement> =
            vec![self.filter_pill(cx, "All", self.source_filter.is_none(), None)];
        for name in &addon_names {
            pills.push(self.filter_pill(
                cx,
                name,
                self.source_filter.as_deref() == Some(name.as_str()),
                Some(name.clone()),
            ));
        }
        sheet = sheet.child(div().flex().flex_row().flex_wrap().gap_2().children(pills));

        let filtered: Vec<&http::Candidate> = sources
            .candidates
            .iter()
            .filter(|c| match &self.source_filter {
                Some(name) => &c.addon == name,
                None => true,
            })
            .collect();
        let body: AnyElement = if filtered.is_empty() {
            div()
                .text_size(px(13.0))
                .text_color(theme::colors::INK_DIM)
                .child("No candidates for this filter.")
                .into_any_element()
        } else {
            let rows: Vec<AnyElement> = filtered
                .into_iter()
                .map(|c| self.candidate_row(c, cx))
                .collect();
            div()
                .flex()
                .flex_col()
                .gap_2()
                .children(rows)
                .into_any_element()
        };
        sheet = sheet.child(body);

        sheet.into_any_element()
    }

    /// One filter pill (All or a single add-on name).
    fn filter_pill(
        &self,
        cx: &mut Context<Self>,
        label: &str,
        active: bool,
        value: Option<String>,
    ) -> AnyElement {
        let id = SharedString::from(format!("filter-{label}"));
        let label = SharedString::from(label.to_owned());
        div()
            .id(id)
            .cursor_pointer()
            .px_3()
            .py_1()
            .rounded_full()
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
            .text_size(px(12.0))
            .on_click(cx.listener(move |state, _ev, _window, cx| {
                state.cycle_source_filter(value.clone());
                cx.notify();
            }))
            .child(label)
            .into_any_element()
    }

    /// One candidate row: quality badge + label + addon/kind/size. Torrent
    /// rows spool on click; direct rows report the honest offline/test-clip
    /// limitation instead.
    fn candidate_row(&self, c: &http::Candidate, cx: &mut Context<Self>) -> AnyElement {
        let quality = c
            .quality
            .clone()
            .filter(|q| !q.is_empty())
            .unwrap_or_else(|| "—".to_string());
        let is_torrent = c.is_torrent();
        let kind = if is_torrent { "torrent" } else { "direct" };
        let label = c.label.clone();
        let addon = c.addon.clone();
        let info = match c.size_bytes.map(format_size) {
            Some(size) => format!("{addon} · {kind} · {size}"),
            None => format!("{addon} · {kind}"),
        };
        let cid = c.id.clone();

        div()
            .id(SharedString::from(format!("candidate-{cid}")))
            .flex()
            .flex_row()
            .items_center()
            .gap_3()
            .px_3()
            .py_2()
            .rounded(theme::radius::CARD)
            .bg(theme::colors::GLASS_TINT)
            .cursor_pointer()
            .hover(|style| style.bg(theme::colors::GLASS_HI))
            .on_click(cx.listener(move |state, _ev, _window, cx| {
                if is_torrent {
                    state.spool_candidate(&cid, cx);
                } else {
                    let msg = "direct source: no spool route (test clip / offline only)";
                    state.status = msg.into();
                    state.sources_status = msg.into();
                    cx.notify();
                }
            }))
            .child(
                div()
                    .px_2()
                    .py_1()
                    .rounded(theme::radius::CARD)
                    .bg(theme::colors::GOLD)
                    .text_color(theme::colors::ON_GOLD)
                    .text_size(px(11.0))
                    .font_weight(FontWeight::BOLD)
                    .child(SharedString::from(quality)),
            )
            .child(
                div()
                    .flex()
                    .flex_col()
                    .gap_1()
                    .child(
                        div()
                            .text_size(px(14.0))
                            .text_color(theme::colors::INK)
                            .child(SharedString::from(label)),
                    )
                    .child(
                        div()
                            .text_size(px(12.0))
                            .text_color(theme::colors::INK_DIM)
                            .child(SharedString::from(info)),
                    ),
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

/// Click target for a search-result row: a live Cinemeta `tt` id (detail-by-
/// imdb) or a seeded series id (existing detail view).
enum SearchTarget {
    Imdb { media_type: String, tt_id: String },
    Series(i64),
}

/// Parse a `COLOSSEUM_UI_START_VIEW` imdb dev hook. `imdb:tt1160419` defaults
/// to `movie`; `imdb:movie:tt…` / `imdb:series:tt…` set the media type
/// explicitly (series ids keep their colon separators inside the id).
fn parse_imdb_hook(start: &str) -> Option<(String, String)> {
    let rest = start.strip_prefix("imdb:")?;
    if rest.is_empty() {
        return None;
    }
    let (media_type, tt_id) = match rest.split_once(':') {
        Some((prefix, id)) if prefix == "movie" || prefix == "series" => {
            (prefix.to_string(), id.to_string())
        }
        _ => ("movie".to_string(), rest.to_string()),
    };
    if tt_id.is_empty() {
        return None;
    }
    Some((media_type, tt_id))
}

/// A one-line per-addon candidate count, e.g. `Torrentio 74 · NoTorrent 3`
/// (most candidates first, ties broken alphabetically).
fn summarize_counts(counts: &BTreeMap<String, usize>) -> String {
    let mut entries: Vec<(&String, &usize)> = counts.iter().collect();
    entries.sort_by(|a, b| b.1.cmp(a.1).then_with(|| a.0.cmp(b.0)));
    let parts: Vec<String> = entries
        .into_iter()
        .map(|(name, n)| format!("{name} {n}"))
        .collect();
    if parts.is_empty() {
        "no sources".to_string()
    } else {
        parts.join(" · ")
    }
}

/// Human-readable byte size for a candidate row's `size_bytes`.
fn format_size(bytes: u64) -> String {
    const GB: f64 = 1024.0 * 1024.0 * 1024.0;
    const MB: f64 = 1024.0 * 1024.0;
    let b = bytes as f64;
    if b >= GB {
        format!("{:.1} GB", b / GB)
    } else if b >= MB {
        format!("{:.0} MB", b / MB)
    } else {
        format!("{bytes} B")
    }
}

fn main() {
    env_logger::init();
    let daemon_url = std::env::var("DAEMON_URL").unwrap_or_else(|_| DAEMON_URL_DEFAULT.to_string());
    let legacy = std::env::var("COLOSSEUM_UI_LEGACY").is_ok_and(|v| v == "1");
    let autoplay = std::env::var("COLOSSEUM_UI_AUTOPLAY").is_ok_and(|v| v == "1");
    let autoplay_source = std::env::var("COLOSSEUM_UI_AUTOPLAY_SOURCE").is_ok_and(|v| v == "1");
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
                    let mut app = CatalogApp::new(
                        daemon_url.clone(),
                        legacy,
                        start_view.clone(),
                        autoplay,
                        autoplay_source,
                    );
                    app.spawn_initial(cx);
                    app
                })
            },
        )
        .unwrap();
        cx.activate(true);
    });
}
