//! GPUI shell for the Colosseum POC: catalog list fed by the daemon over
//! HTTP, video playback via gpui-video-player (GStreamer). This is the
//! playback spike — proving GPUI holds the whole UI together — not a visual
//! port of the QML design language.

use gpui::prelude::*;
use gpui::{
    div, px, Application, Context, InteractiveElement, IntoElement, ParentElement, Render,
    SharedString, Styled, TitlebarOptions, Window, WindowBounds, WindowOptions,
};
use gpui_video_player::{video, Video};
use serde::Deserialize;
use url::Url;

const DAEMON_URL_DEFAULT: &str = "http://127.0.0.1:8123";
const TEST_CLIP: &str = "/tmp/colosseum-ui-test.mp4";

#[derive(Clone, Deserialize)]
struct Series {
    id: i64,
    title: String,
    #[allow(dead_code)]
    source: String,
}

struct CatalogApp {
    series: Vec<Series>,
    selected: Option<usize>,
    playing: Option<Video>,
    status: SharedString,
}

impl CatalogApp {
    fn fetch_catalog(daemon_url: &str) -> Result<Vec<Series>, String> {
        let url = format!("{daemon_url}/catalog/search?q=");
        let body = ureq::get(&url)
            .timeout(std::time::Duration::from_secs(3))
            .call()
            .map_err(|e| format!("daemon unreachable: {e}"))?
            .into_string()
            .map_err(|e| format!("read failed: {e}"))?;
        serde_json::from_str(&body).map_err(|e| format!("bad catalog JSON: {e}"))
    }
}

impl Render for CatalogApp {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let root = div()
            .size_full()
            .flex()
            .flex_row()
            .bg(gpui::hsl(0.6, 0.08, 0.08))
            .text_color(gpui::hsl(0.12, 0.2, 0.92))
            .font_family(".SystemUIFont")
            // left pane: catalog
            .child(
                div()
                    .flex()
                    .flex_col()
                    .w(px(320.0))
                    .h_full()
                    .p_4()
                    .gap_2()
                    .border_r_1()
                    .border_color(gpui::hsl(0.6, 0.1, 0.2))
                    .child(
                        div()
                            .text_size(px(20.0))
                            .font_weight(gpui::FontWeight::BOLD)
                            .child("Colosseum"),
                    )
                    .child(
                        div()
                            .text_size(px(12.0))
                            .text_color(gpui::hsl(0.6, 0.1, 0.55))
                            .child(self.status.clone()),
                    )
                    .children(self.series.iter().enumerate().map(|(ix, s)| {
                        let selected = self.selected == Some(ix);
                        div()
                            .id(("series", ix))
                            .py_2()
                            .px_3()
                            .rounded_md()
                            .cursor_pointer()
                            .bg(if selected {
                                gpui::hsl(0.6, 0.3, 0.22)
                            } else {
                                gpui::transparent_black()
                            })
                            .hover(|s| s.bg(gpui::hsl(0.6, 0.25, 0.16)))
                            .on_click(cx.listener(move |state, _ev, _window, cx| {
                                state.selected = Some(ix);
                                let uri = Url::from_file_path(TEST_CLIP)
                                    .expect("test clip path is absolute");
                                match Video::new(&uri) {
                                    Ok(v) => {
                                        state.status =
                                            format!("playing test clip for '{}'", s.title).into();
                                        state.playing = Some(v);
                                    }
                                    Err(e) => state.status = format!("video init failed: {e}").into(),
                                }
                                cx.notify();
                            }))
                            .child(SharedString::from(s.title.clone()))
                    })),
            );

        // right pane: player
        let player = match &self.playing {
            Some(v) => video(v.clone()).id("player").into_any_element(),
            None => div()
                .flex_1()
                .flex()
                .items_center()
                .justify_center()
                .text_color(gpui::hsl(0.6, 0.1, 0.45))
                .child("select a series to play the test clip")
                .into_any_element(),
        };

        root.child(
            div()
                .flex_1()
                .h_full()
                .flex()
                .items_center()
                .justify_center()
                .overflow_hidden()
                .child(player),
        )
    }
}

fn main() {
    env_logger::init();
    let daemon_url =
        std::env::var("DAEMON_URL").unwrap_or_else(|_| DAEMON_URL_DEFAULT.to_string());

    Application::new().run(move |cx| {
        let bounds = WindowBounds::Windowed(gpui::Bounds {
            origin: gpui::Point::new(px(120.0), px(120.0)),
            size: gpui::Size {
                width: px(1280.0),
                height: px(800.0),
            },
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
                    cx.spawn(|this, mut cx| async move {
                        let fetched =
                            cx.background_executor()
                                .spawn(async move { CatalogApp::fetch_catalog(&daemon_url) })
                                .await;
                        this.update(&mut cx, |app, cx| match fetched {
                            Ok(series) => {
                                app.status = format!("{} series from daemon", series.len()).into();
                                app.series = series;
                            }
                            Err(e) => app.status = SharedString::from(e),
                        })
                        .ok();
                        cx.notify();
                    })
                    .detach();
                    CatalogApp {
                        series: Vec::new(),
                        selected: None,
                        playing: None,
                        status: "connecting to daemon…".into(),
                    }
                })
            },
        )
        .unwrap();
        cx.activate(true);
    });
}
