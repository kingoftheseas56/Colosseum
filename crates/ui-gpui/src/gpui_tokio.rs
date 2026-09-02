//! Vendored from zed-industries/zed `crates/gpui_tokio` (Apache-2.0), with
//! one adaptation: the `gpui_util::defer` abort guard is inlined so the
//! vendored file has no zed-internal dependencies. Upstream link:
//! https://github.com/zed-industries/zed/blob/main/crates/gpui_tokio/src/gpui_tokio.rs
//!
//! Bridges tokio futures onto gpui's executor: the future runs on a small
//! tokio runtime, and is aborted if the returned gpui task is dropped.

use std::future::Future;

use gpui::{App, AppContext, AsyncApp, Global, Task};
pub use tokio::task::JoinError;

/// Initializes the Tokio wrapper using a new Tokio runtime with 2 worker threads.
pub fn init(cx: &mut App) {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .enable_all()
        .build()
        .expect("Failed to initialize Tokio");

    let handle = runtime.handle().clone();
    cx.set_global(GlobalTokio {
        owned_runtime: Some(runtime),
        handle,
    });
}

struct GlobalTokio {
    owned_runtime: Option<tokio::runtime::Runtime>,
    handle: tokio::runtime::Handle,
}

impl Global for GlobalTokio {}

impl Drop for GlobalTokio {
    fn drop(&mut self) {
        if let Some(runtime) = self.owned_runtime.take() {
            runtime.shutdown_background();
        }
    }
}

struct AbortOnDrop(tokio::task::AbortHandle);

impl Drop for AbortOnDrop {
    fn drop(&mut self) {
        self.0.abort();
    }
}

pub struct Tokio {}

impl Tokio {
    /// Spawns the given future on Tokio's thread pool, and returns it via a GPUI task.
    /// The Tokio task will be cancelled if the GPUI task is dropped.
    pub fn spawn<Fut, R>(cx: &mut AsyncApp, f: Fut) -> Task<Result<R, JoinError>>
    where
        Fut: Future<Output = R> + Send + 'static,
        R: Send + 'static,
    {
        let handle = cx
            .read_global(|tokio: &GlobalTokio, _app| tokio.handle.clone())
            .expect("gpui_tokio::init was not called");
        let join_handle = handle.spawn(f);
        let cancel = AbortOnDrop(join_handle.abort_handle());
        cx.background_spawn(async move {
            let result = join_handle.await;
            drop(cancel);
            result
        })
    }
}
