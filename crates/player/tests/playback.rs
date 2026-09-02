//! End-to-end playback spec for the native backend (macOS).
//! Generates a small clip with ffmpeg if missing, then requires real
//! decoded frames to arrive through the `Player` contract.

use std::time::{Duration, Instant};

#[test]
#[cfg(target_os = "macos")]
fn native_backend_decodes_frames() {
    let clip = "/tmp/colosseum-ui-test-small.mp4";
    if !std::path::Path::new(clip).exists() {
        let status = std::process::Command::new("ffmpeg")
            .args([
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-f",
                "lavfi",
                "-i",
                "testsrc2=size=320x180:rate=10:duration=3",
                "-f",
                "lavfi",
                "-i",
                "sine=frequency=440",
                "-c:v",
                "libx264",
                "-pix_fmt",
                "yuv420p",
                "-c:a",
                "aac",
                "-shortest",
                clip,
            ])
            .status()
            .expect("ffmpeg must be installed");
        assert!(status.success(), "ffmpeg failed to make the clip");
    }

    let mut player = player::native().expect("native() on macOS = avfoundation");
    player
        .load(&format!("file://{clip}"))
        .expect("load local file");

    // AVPlayer autoplays on load (player_thread calls play()), so just poll.
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut frame_count = 0u32;
    let mut dims = None;
    while Instant::now() < deadline {
        if let Some(f) = player.next_frame() {
            frame_count += 1;
            dims = Some((f.width, f.height, f.bgra.len()));
            if frame_count >= 5 {
                break;
            }
        }
        std::thread::sleep(Duration::from_millis(25));
    }

    assert!(
        frame_count >= 1,
        "expected >=1 decoded frame, got {frame_count}"
    );
    let (w, h, len) = dims.expect("dims from a frame");
    assert_eq!((w, h), (320, 180), "unexpected frame dimensions");
    assert_eq!(len, (w * h * 4) as usize, "BGRA stride must be tight");
    let d = player.duration().unwrap_or(0.0);
    assert!(
        (2.5..=4.0).contains(&d),
        "duration {d}s outside 2.5..4 for a 3s clip"
    );
}
