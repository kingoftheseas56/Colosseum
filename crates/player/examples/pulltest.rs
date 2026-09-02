fn main() {
    let mut p = player::native().unwrap();
    p.load("file:///tmp/colosseum-ui-test.mp4").unwrap();
    let start = std::time::Instant::now();
    let mut n = 0u32;
    let mut last_pts = 0.0f64;
    while start.elapsed().as_secs_f64() < 3.0 {
        if p.next_frame().is_some() {
            n += 1;
        } else {
            std::thread::sleep(std::time::Duration::from_millis(5));
        }
        last_pts = p.position();
    }
    println!("frames={n} last_pts={last_pts:.3} dur={:?}", p.duration());
}
