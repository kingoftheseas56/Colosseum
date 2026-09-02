//! macOS backend: AVPlayer + AVPlayerItemVideoOutput → CVPixelBuffer →
//! BGRA frames. Implemented by the player-avfoundation work item.

use crate::{PlayerError, VideoFrame};

pub struct AvFoundationPlayer {
    _private: (),
}

impl AvFoundationPlayer {
    pub fn new() -> Result<Self, PlayerError> {
        Ok(Self { _private: () })
    }
}

impl crate::Player for AvFoundationPlayer {
    fn load(&mut self, _url: &str) -> Result<(), PlayerError> {
        Err(PlayerError::Backend(
            "not yet implemented (player-avfoundation)".into(),
        ))
    }
    fn play(&mut self) {}
    fn pause(&mut self) {}
    fn seek(&mut self, _position_secs: f64) {}
    fn position(&self) -> f64 {
        0.0
    }
    fn duration(&self) -> Option<f64> {
        None
    }
    fn next_frame(&mut self) -> Option<VideoFrame> {
        None
    }
    fn event(&mut self) -> Option<crate::PlayerEvent> {
        None
    }
}
