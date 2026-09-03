//! End-to-end playback specs for the native backend (macOS).
//! Generates a small clip with ffmpeg if missing, then requires real
//! decoded frames to arrive through the `Player` contract from a `file://`
//! URL, and documents the `http(s)://` transport boundary: a real local HTTP
//! server with `Range` support is reachable, but AVAssetReader cannot read
//! remote assets, so `load()` must report that honestly instead of faking
//! frames (see the module docs in `avfoundation.rs`).

use std::time::{Duration, Instant};

fn ensure_clip() -> &'static str {
    const CLIP: &str = "/tmp/colosseum-ui-test-small.mp4";
    if !std::path::Path::new(CLIP).exists() {
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
                CLIP,
            ])
            .status()
            .expect("ffmpeg must be installed");
        assert!(status.success(), "ffmpeg failed to make the clip");
    }
    CLIP
}

#[test]
#[cfg(target_os = "macos")]
fn native_backend_decodes_frames() {
    let clip = ensure_clip();

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

// ── minimal single-file HTTP server with `Range` support ────────────────────
// AVFoundation's progressive http reader needs byte-range requests to fetch the
// moov atom (which ffmpeg leaves at the end of the file). A std::net
// TcpListener is enough — no HTTP stack dependency for a test. It records the
// byte ranges it serves and the total connection count so tests can prove the
// transport boundary was genuinely exercised.

#[cfg(target_os = "macos")]
mod http_helpers {
    use std::io::{ErrorKind, Read, Write};
    use std::net::{TcpListener, TcpStream};
    use std::path::Path;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::{Arc, Mutex};
    use std::thread;
    use std::time::Duration;

    pub struct RangeServer {
        port: u16,
        ranges: Arc<Mutex<Vec<(u64, u64)>>>,
        requests: Arc<AtomicUsize>,
        shutdown: Arc<AtomicBool>,
        handle: Option<thread::JoinHandle<()>>,
    }

    impl RangeServer {
        pub fn serve_file(path: &Path) -> RangeServer {
            let data = std::fs::read(path).expect("read clip bytes");
            let listener = TcpListener::bind("127.0.0.1:0").expect("bind ephemeral port");
            let port = listener.local_addr().unwrap().port();
            let ranges = Arc::new(Mutex::new(Vec::new()));
            let requests = Arc::new(AtomicUsize::new(0));
            let shutdown = Arc::new(AtomicBool::new(false));

            let sh = Arc::clone(&shutdown);
            let rg = Arc::clone(&ranges);
            let rq = Arc::clone(&requests);
            let handle = thread::spawn(move || {
                listener
                    .set_nonblocking(true)
                    .expect("nonblocking listener");
                while !sh.load(Ordering::Relaxed) {
                    match listener.accept() {
                        Ok((stream, _)) => {
                            rq.fetch_add(1, Ordering::Relaxed);
                            let _ = serve_one(stream, &data, &rg);
                        }
                        Err(ref e) if e.kind() == ErrorKind::WouldBlock => {
                            thread::sleep(Duration::from_millis(5));
                        }
                        Err(_) => break,
                    }
                }
            });

            RangeServer {
                port,
                ranges,
                requests,
                shutdown,
                handle: Some(handle),
            }
        }

        pub fn host_port(&self) -> String {
            format!("127.0.0.1:{}", self.port)
        }

        pub fn url(&self) -> String {
            format!("http://{}/clip.mp4", self.host_port())
        }

        /// Every byte-range this server has served, in request order.
        pub fn ranges(&self) -> Vec<(u64, u64)> {
            self.ranges.lock().unwrap().clone()
        }

        /// Total number of accepted connections.
        pub fn request_count(&self) -> usize {
            self.requests.load(Ordering::Relaxed)
        }
    }

    impl Drop for RangeServer {
        fn drop(&mut self) {
            self.shutdown.store(true, Ordering::Relaxed);
            if let Some(h) = self.handle.take() {
                let _ = h.join();
            }
        }
    }

    /// Issue a raw `GET` with a byte-range against the server; returns the HTTP
    /// status code and the response body. Proves Range support independent of
    /// AVFoundation.
    pub fn get_range(host_port: &str, path: &str, start: u64, end: u64) -> (u16, Vec<u8>) {
        let mut stream = TcpStream::connect(host_port).expect("connect to range server");
        write!(
            stream,
            "GET {path} HTTP/1.1\r\nHost: {host_port}\r\nRange: bytes={start}-{end}\r\nConnection: close\r\n\r\n"
        )
        .expect("write request");
        let mut buf = Vec::new();
        stream.read_to_end(&mut buf).expect("read response");
        let head = String::from_utf8_lossy(&buf);
        let status: u16 = head
            .lines()
            .next()
            .and_then(|l| l.split_whitespace().nth(1))
            .and_then(|code| code.parse().ok())
            .unwrap_or(0);
        let body_start = buf
            .windows(4)
            .position(|w| w == b"\r\n\r\n")
            .map(|i| i + 4)
            .unwrap_or(buf.len());
        (status, buf[body_start..].to_vec())
    }

    /// Parse a `Range` header value (`bytes=start-end` / `bytes=start-` /
    /// `bytes=-suffix`) into a concrete inclusive `(start, end)` byte range.
    fn parse_range(spec: &str, total: u64) -> Option<(u64, u64)> {
        if total == 0 {
            return None;
        }
        let last = total - 1;
        let (a, b) = spec.split_once('-')?;
        if a.is_empty() {
            // suffix: last N bytes
            let n: u64 = b.parse().ok()?;
            if n == 0 {
                return None;
            }
            let start = total.saturating_sub(n);
            return Some((start, last));
        }
        let start: u64 = a.parse().ok()?;
        if start >= total {
            return None;
        }
        let end: u64 = if b.is_empty() { last } else { b.parse().ok()? };
        if end < start {
            return None;
        }
        Some((start, end.min(last)))
    }

    fn serve_one(
        mut stream: TcpStream,
        data: &[u8],
        log: &Mutex<Vec<(u64, u64)>>,
    ) -> std::io::Result<()> {
        // Read the request head (request line + headers), not the body.
        let mut head = Vec::with_capacity(1024);
        let mut buf = [0u8; 1024];
        loop {
            let n = stream.read(&mut buf)?;
            if n == 0 {
                break;
            }
            head.extend_from_slice(&buf[..n]);
            if head.windows(4).any(|w| w == b"\r\n\r\n") || head.len() > 64 * 1024 {
                break;
            }
        }
        let head = String::from_utf8_lossy(&head);
        let mut lines = head.split("\r\n");
        let request_line = lines.next().unwrap_or("");
        let mut parts = request_line.split_whitespace();
        let method = parts.next().unwrap_or("");
        let _path = parts.next();

        let mut range: Option<(u64, u64)> = None;
        for line in lines {
            let Some((key, value)) = line.split_once(':') else {
                continue;
            };
            if key.eq_ignore_ascii_case("range") {
                let spec = value
                    .trim()
                    .strip_prefix("bytes=")
                    .unwrap_or_else(|| value.trim());
                range = parse_range(spec, data.len() as u64);
            }
        }

        if method != "GET" && method != "HEAD" {
            let body = b"method not allowed";
            write!(
                stream,
                "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                body.len()
            )?;
            stream.write_all(body)?;
            return Ok(());
        }

        let total = data.len() as u64;
        match range {
            Some((start, end)) => {
                log.lock().unwrap().push((start, end));
                let len = end - start + 1;
                write!(
                    stream,
                    "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes {start}-{end}/{total}\r\nAccept-Ranges: bytes\r\nContent-Length: {len}\r\nContent-Type: video/mp4\r\nConnection: close\r\n\r\n"
                )?;
                if method == "GET" {
                    stream.write_all(&data[start as usize..=end as usize])?;
                }
            }
            None => {
                write!(
                    stream,
                    "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nContent-Length: {total}\r\nContent-Type: video/mp4\r\nConnection: close\r\n\r\n"
                )?;
                if method == "GET" {
                    stream.write_all(data)?;
                }
            }
        }
        Ok(())
    }
}

#[test]
#[cfg(target_os = "macos")]
fn http_server_supports_byte_ranges() {
    let clip = ensure_clip();
    let server = http_helpers::RangeServer::serve_file(std::path::Path::new(clip));
    let (status, body) = http_helpers::get_range(&server.host_port(), "/clip.mp4", 100, 200);
    assert_eq!(status, 206, "expected 206 Partial Content");
    let file = std::fs::read(clip).expect("read clip bytes");
    assert_eq!(
        &body[..],
        &file[100..=200],
        "range bytes must match the file"
    );
    // The server must have recorded exactly that byte range — this is what a
    // loopback torrent sidecar must implement for AVFoundation seeks.
    assert_eq!(
        server.ranges(),
        vec![(100, 200)],
        "server must log the byte range"
    );
}

#[test]
#[cfg(target_os = "macos")]
fn http_backend_reports_remote_not_supported() {
    let clip = ensure_clip();
    let server = http_helpers::RangeServer::serve_file(std::path::Path::new(clip));
    let url = server.url();

    let mut player = player::native().expect("native() on macOS = avfoundation");
    // Do NOT fake success: AVAssetReader cannot read remote assets, so load()
    // must fail with the documented seam rather than return an empty player.
    let err = player
        .load(&url)
        .expect_err("http load must fail for the AVAssetReader backend, not fake frames");
    let msg = err.to_string();
    assert!(
        msg.contains("not readable by AVAssetReader"),
        "error should name the AVAssetReader seam, got: {msg}"
    );
    assert!(
        msg.contains("-11838"),
        "error should carry AVErrorOperationNotSupportedForAsset (-11838), got: {msg}"
    );

    // The http transport boundary was genuinely exercised before the rejection:
    // metadata (tracks/duration) loaded over http and the server answered.
    assert!(
        server.request_count() > 0,
        "http load should have hit the server at least once"
    );
}

#[test]
#[cfg(target_os = "macos")]
fn load_rejects_unknown_schemes() {
    let mut player = player::native().expect("native() on macOS = avfoundation");
    let err = player
        .load("magnet:?xt=urn:btih:0123456789abcdef")
        .expect_err("torrent/magnet links must be rejected at the transport boundary");
    assert!(
        err.to_string().contains("unsupported source"),
        "magnet link should be UnsupportedSource, got: {err}"
    );
}
