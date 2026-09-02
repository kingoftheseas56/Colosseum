//! Player domain: per-OS native video playback behind one contract.
//!
//! Backends: macOS AVFoundation (objc2), Windows Media Foundation
//! (windows-rs), Linux GStreamer (gstreamer-rs) — each OS's own decoder
//! pipeline. Frame delivery starts as BGRA8 software frames (RenderImage-
//! compatible); zero-copy IOSurface/DXGI interop is a later optimization
//! pass behind this same contract.
//!
//! Contract: `Player` + `VideoFrame` + `PlayerEvent`. Pull-based by design —
//! backends keep only the newest decoded frame (`next_frame` is latest-wins
//! backpressure), UI pumps at its render cadence.

use thiserror::Error;

#[derive(Debug, Error)]
pub enum PlayerError {
    #[error("unsupported source: {0}")]
    UnsupportedSource(String),
    #[error("backend error: {0}")]
    Backend(String),
    #[error("backend not ported to this platform yet: {0}")]
    NotPorted(&'static str),
}

/// One decoded frame, tightly packed BGRA8, row stride == width * 4.
pub struct VideoFrame {
    pub bgra: Vec<u8>,
    pub width: u32,
    pub height: u32,
    pub timestamp_secs: f64,
}

#[derive(Debug)]
pub enum PlayerEvent {
    Ended,
    Failed(String),
}

pub trait Player: Send {
    fn load(&mut self, url: &str) -> Result<(), PlayerError>;
    fn play(&mut self);
    fn pause(&mut self);
    fn seek(&mut self, position_secs: f64);
    /// Latest known position; may lag by a frame. Seconds.
    fn position(&self) -> f64;
    fn duration(&self) -> Option<f64>;
    /// Newest decoded frame, if one arrived since the last call.
    fn next_frame(&mut self) -> Option<VideoFrame>;
    /// Oldest un-consumed event, if any.
    fn event(&mut self) -> Option<PlayerEvent>;
}

/// The native backend for this platform.
pub fn native() -> Result<Box<dyn Player>, PlayerError> {
    #[cfg(target_os = "macos")]
    {
        Ok(Box::new(crate::avfoundation::AvFoundationPlayer::new()?))
    }
    #[cfg(not(target_os = "macos"))]
    {
        Err(PlayerError::NotPorted(backend_name()))
    }
}

pub fn backend_name() -> &'static str {
    if cfg!(target_os = "macos") {
        "avfoundation"
    } else if cfg!(target_os = "windows") {
        "media-foundation"
    } else {
        "gstreamer"
    }
}

#[cfg(target_os = "macos")]
mod avfoundation;
