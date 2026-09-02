//! GPUI shell for the Colosseum POC: catalog list fed by the daemon over
//! HTTP, video frames from crates/player's native backend painted as
//! RenderImage at GPUI's repaint cadence. This is the playback spike —
//! proving GPUI holds the whole UI together with platform-native video
//! behind the Player abstraction — not a visual port of the QML design
//! language.

mod gpui_tokio;

use std::sync::Arc;

use gpui::prelude::*;
use gpui::{
    div, img, px, size, Application, Context, InteractiveElement, IntoElement, ObjectFit,
    ParentElement, Render, SharedString, Styled, TitlebarOptions, Window, WindowBounds,
    WindowOptions,
};
use player::{Player, VideoFrame};
use serde::Deserialize;

const DAEMON_URL_DEFAULT: &str = "http://127.0.0.1:8123";
const TEST_CLIP: &str = "file:///tmp/colosseum-ui-test.mp4";

#[derive(Clone, Deserialize)]
struct Series {
    #[allow(dead_code)]
    id: i64,
    title: String,
    #[allow(dead_code)]
    source: String,
}

struct CatalogApp {
    series: Vec<Series>,
    selected: Option<usize>,
    player: Option<Box<dyn Player>>,
    frame: Option<Arc<gpui::RenderImage>>,
    status: SharedString,
}

impl CatalogApp {
    async fn fetch_catalog(daemon_url: &str) -> Result<Vec<Series>, String> {
        let url = format!("{daemon_url}/catalog/search?q=");
        let body = reqwest::get(&url)
            .await
            .map_err(|e| format!("daemon unreachable: {e}"))?
            .text()
            .await
            .map_err(|e| format!("read failed: {e}"))?;
        serde_json::from_str(&body).map_err(|e| format!("bad catalog JSON: {e}"))
    }

    fn start_playback(&mut self, cx: &mut Context<Self>, label: &str, url: &str) {
        match player::native() {
            Ok(mut player) => {
                if let Err(e) = player.load(url) {
                    self.status = format!("{label}: load failed: {e}").into();
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

impl Render for CatalogApp {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        // Note: frames arrive via the timer-driven pump started in
        // start_pump(); render only paints the latest frame.

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
                            .font_weight(gpui::FontWeight::BOLD)
                            .child("Colosseum"),
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
                            .hover(|s| s.bg(gpui::hsla(0.6, 0.25, 0.16, 1.0)))
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
    }
}

fn main() {
    env_logger::init();
    let daemon_url = std::env::var("DAEMON_URL").unwrap_or_else(|_| DAEMON_URL_DEFAULT.to_string());

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
                    cx.spawn(async move |this, cx| {
                        let fetched = gpui_tokio::Tokio::spawn(cx, async move {
                            CatalogApp::fetch_catalog(&daemon_url).await
                        })
                        .await
                        .unwrap_or_else(|e| Err(format!("tokio join failed: {e}")));
                        let _ = this.update(cx, |app: &mut CatalogApp, cx| {
                            match fetched {
                                Ok(series) => {
                                    app.status =
                                        format!("{} series from daemon", series.len()).into();
                                    app.series = series;
                                    // autoplay on launch keeps the spike testable
                                    // without a human click
                                    if !app.series.is_empty() {
                                        app.selected = Some(0);
                                        let title = app.series[0].title.clone();
                                        app.start_playback(cx, &title, TEST_CLIP);
                                    }
                                }
                                Err(e) => app.status = SharedString::from(e),
                            }
                            cx.notify();
                        });
                    })
                    .detach();
                    CatalogApp {
                        series: Vec::new(),
                        selected: None,
                        player: None,
                        frame: None,
                        status: "connecting to daemon…".into(),
                    }
                })
            },
        )
        .unwrap();
        cx.activate(true);
    });
}
