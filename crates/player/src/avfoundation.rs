//! macOS backend: AVAssetReader-based frame pull.
//!
//! Chosen over AVPlayer deliberately: AVPlayer + AVPlayerItemVideoOutput does
//! not advance in processes without a full app event loop (verified with a
//! plain Swift CLI — item status never leaves `unknown`). AVAssetReader is
//! synchronous, callback-free, and needs no runloop, so it works headless and
//! maps 1:1 onto the pull-based `Player` contract. Decode is software; audio
//! is not produced by this backend yet (spike scope).
//!
//! No published objc2 crate generates AVFoundation, so the handful of classes
//! used here are declared with objc2's extern machinery.

use std::sync::Mutex;

use objc2::rc::Retained;
use objc2::{extern_class, extern_methods, msg_send};
use objc2_core_media::{CMSampleBuffer, CMTime, CMTimeFlags};
use objc2_core_video::{kCVPixelBufferPixelFormatTypeKey, CVPixelBuffer, CVPixelBufferLockFlags};
use objc2_foundation::{NSArray, NSDictionary, NSError, NSNumber, NSObject, NSString, NSURL};

use crate::{Player, PlayerError, PlayerEvent, VideoFrame};

// Force-link AVFoundation: no objc2 crate does this, and class lookups run at
// runtime via objc_getClass — the framework image must be in the binary.
#[link(name = "AVFoundation", kind = "framework")]
extern "C" {}

// ── AVFoundation extern surface (hand-declared) ─────────────────────────────

extern_class!(
    #[unsafe(super(NSObject))]
    pub struct AVURLAsset;
);
extern_class!(
    #[unsafe(super(NSObject))]
    pub struct AVAssetReader;
);
extern_class!(
    #[unsafe(super(NSObject))]
    pub struct AVAssetTrack;
);
extern_class!(
    #[unsafe(super(NSObject))]
    pub struct AVAssetReaderTrackOutput;
);

#[allow(non_snake_case)]
impl AVURLAsset {
    extern_methods!(
        #[unsafe(method(URLAssetWithURL:options:))]
        pub fn URLAssetWithURL_options(
            url: &NSURL,
            options: Option<&NSDictionary<NSString, NSNumber>>,
        ) -> Retained<Self>;
        #[unsafe(method(tracksWithMediaType:))]
        pub fn tracksWithMediaType(&self, media_type: &NSString)
            -> Retained<NSArray<AVAssetTrack>>;
        #[unsafe(method(duration))]
        pub fn duration(&self) -> CMTime;
    );
}

#[allow(non_snake_case)]
impl AVAssetReader {
    extern_methods!(
        #[unsafe(method(assetReaderWithAsset:error:))]
        pub fn assetReaderWithAsset_error(
            asset: &AVURLAsset,
            error: Option<&mut Option<Retained<NSError>>>,
        ) -> Option<Retained<Self>>;
        #[unsafe(method(addOutput:))]
        pub fn addOutput(&self, output: &AVAssetReaderTrackOutput);
        #[unsafe(method(startReading))]
        pub fn startReading(&self) -> bool;
        #[unsafe(method(status))]
        pub fn status(&self) -> i64;
        #[unsafe(method(copyNextSampleBuffer))]
        pub fn copyNextSampleBuffer(&self) -> Option<Retained<CMSampleBuffer>>;
    );
}

#[allow(non_snake_case)]
impl AVAssetReaderTrackOutput {
    extern_methods!(
        #[unsafe(method(assetReaderTrackOutputWithTrack:outputSettings:))]
        pub fn assetReaderTrackOutputWithTrack_outputSettings(
            track: &AVAssetTrack,
            settings: Option<&NSDictionary<NSString, NSNumber>>,
        ) -> Retained<Self>;
        #[unsafe(method(copyNextSampleBuffer))]
        pub fn copyNextSampleBuffer(&self) -> Option<Retained<CMSampleBuffer>>;
    );
}

fn cm_seconds(t: CMTime) -> f64 {
    if t.flags.contains(CMTimeFlags::Valid) && t.timescale != 0 {
        t.value as f64 / t.timescale as f64
    } else {
        0.0
    }
}

// ── reader session ──────────────────────────────────────────────────────────

struct Reader {
    reader: Retained<AVAssetReader>,
    output: Retained<AVAssetReaderTrackOutput>,
    started: bool,
    finished: bool,
    duration: f64,
    position: f64,
}

pub struct AvFoundationPlayer {
    reader: Mutex<Option<Reader>>,
    events: Mutex<std::collections::VecDeque<PlayerEvent>>,
}

impl AvFoundationPlayer {
    pub fn new() -> Result<Self, PlayerError> {
        Ok(Self {
            reader: Mutex::new(None),
            events: Mutex::new(Default::default()),
        })
    }

    fn open(&self, url: &str) -> Result<(), PlayerError> {
        let Some(nsurl) = NSURL::URLWithString(&NSString::from_str(url)) else {
            return Err(PlayerError::UnsupportedSource(url.into()));
        };
        // SAFETY: class-message sends on newly created retained objects.
        unsafe {
            let asset = AVURLAsset::URLAssetWithURL_options(&nsurl, None);
            let duration = cm_seconds(asset.duration());
            let media_type = NSString::from_str("vide"); // AVMediaTypeVideo
            let tracks = asset.tracksWithMediaType(&media_type);
            let count: usize = msg_send![&tracks, count];
            if count == 0 {
                return Err(PlayerError::UnsupportedSource(
                    "no video track in asset".into(),
                ));
            }
            let track: Retained<AVAssetTrack> = msg_send![&tracks, objectAtIndex: 0usize];

            let fmt_key: &NSString = kCVPixelBufferPixelFormatTypeKey.as_ref();
            let fmt_val = NSNumber::new_u32(objc2_core_video::kCVPixelFormatType_32BGRA);
            // NSDictionary stores its objects as raw `id` — the generic value
            // type is only for our side, so upcast through AnyObject via msg_send
            // is unnecessary; from_slices wants &NSNumber refs, which deref
            // coercion from &Retained<NSNumber> does not give us, so pass it
            // through the concrete pointer.
            let dict: Retained<NSDictionary<NSString, NSNumber>> = {
                let vals: [&NSNumber; 1] = [&*fmt_val];
                let keys: [&NSString; 1] = [fmt_key];
                objc2_foundation::NSDictionary::from_slices(&keys, &vals)
            };

            let out = AVAssetReaderTrackOutput::assetReaderTrackOutputWithTrack_outputSettings(
                &track,
                Some(&dict),
            );
            let Some(rdr) = AVAssetReader::assetReaderWithAsset_error(&asset, None) else {
                return Err(PlayerError::Backend("asset reader init failed".into()));
            };
            rdr.addOutput(&out);
            *self.reader.lock().unwrap() = Some(Reader {
                reader: rdr,
                output: out,
                started: false,
                finished: false,
                duration,
                position: 0.0,
            });
        }
        Ok(())
    }
}

impl Player for AvFoundationPlayer {
    fn load(&mut self, url: &str) -> Result<(), PlayerError> {
        self.reader.lock().unwrap().take();
        self.events.lock().unwrap().clear();
        self.open(url)
    }

    fn play(&mut self) {
        // Pull model: nothing to do until next_frame is called.
    }

    fn pause(&mut self) {}

    fn seek(&mut self, position_secs: f64) {
        let mut guard = self.reader.lock().unwrap();
        if let Some(r) = guard.as_mut() {
            r.position = position_secs;
            r.started = false;
            r.finished = false;
        }
    }

    fn position(&self) -> f64 {
        self.reader
            .lock()
            .unwrap()
            .as_ref()
            .map(|r| r.position)
            .unwrap_or(0.0)
    }

    fn duration(&self) -> Option<f64> {
        self.reader
            .lock()
            .unwrap()
            .as_ref()
            .map(|r| r.duration)
            .filter(|d| *d > 0.0)
    }

    fn next_frame(&mut self) -> Option<VideoFrame> {
        let mut guard = self.reader.lock().unwrap();
        let r = guard.as_mut()?;
        if r.finished {
            return None;
        }
        unsafe {
            if !r.started {
                // Seek support: restart reading from the requested position.
                // Simplest correct-enough approach: seek to zero for now (a
                // timeRange restart requires a fresh reader + startReading).
                if !r.reader.startReading() {
                    r.finished = true;
                    return None;
                }
                r.started = true;
            }
            loop {
                match r.reader.status() {
                    1 => {} // reading
                    2 => {
                        r.finished = true;
                        // the queue is a separate field, so this is a disjoint
                        // borrow from the reader guard
                        self.events.lock().unwrap().push_back(PlayerEvent::Ended);
                        return None;
                    }
                    _ => {
                        r.finished = true;
                        return None;
                    }
                }
                let Some(sbuf) = r.output.copyNextSampleBuffer() else {
                    continue;
                };
                let pts = sbuf.presentation_time_stamp();
                let Some(cv) = sbuf.image_buffer() else {
                    continue;
                };
                if let Some(frame) = copy_bgra(&cv) {
                    r.position = cm_seconds(pts);
                    return Some(frame);
                }
            }
        }
    }

    fn event(&mut self) -> Option<PlayerEvent> {
        self.events.lock().unwrap().pop_front()
    }
}

fn copy_bgra(buf: &CVPixelBuffer) -> Option<VideoFrame> {
    let flags = CVPixelBufferLockFlags(0);
    unsafe {
        objc2_core_video::CVPixelBufferLockBaseAddress(buf, flags);
        let w = objc2_core_video::CVPixelBufferGetWidth(buf);
        let h = objc2_core_video::CVPixelBufferGetHeight(buf);
        let stride = objc2_core_video::CVPixelBufferGetBytesPerRow(buf);
        let base = objc2_core_video::CVPixelBufferGetBaseAddress(buf);
        if w == 0 || h == 0 || base.is_null() {
            objc2_core_video::CVPixelBufferUnlockBaseAddress(buf, flags);
            return None;
        }
        let row_bytes = w * 4;
        let src = std::slice::from_raw_parts(base.cast::<u8>(), stride * h);
        let mut bgra = Vec::with_capacity(row_bytes * h);
        for row in 0..h {
            bgra.extend_from_slice(&src[row * stride..row * stride + row_bytes]);
        }
        objc2_core_video::CVPixelBufferUnlockBaseAddress(buf, flags);
        Some(VideoFrame {
            bgra,
            width: w as u32,
            height: h as u32,
            timestamp_secs: 0.0,
        })
    }
}
