"""Build deterministic local media matching the legacy route test contract."""
import pathlib
import subprocess
import sys

candidate, fixtures, output = sys.argv[1:]
output = pathlib.Path(output)
output.mkdir(parents=True, exist_ok=True)
media = output / "legacy-240p-h264.mp4"
fallback = output / "legacy-240p-mpeg4.mp4"
subprocess.run([
    "/usr/bin/ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
    "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25",
    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
    "-t", "7", "-c:v", "libx264", "-g", "50", "-bf", "0",
    "-sc_threshold", "0", "-c:a", "aac", str(media),
], check=True)
subprocess.run([
    "/usr/bin/ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
    "-i", str(media), "-c:v", "mpeg4", "-c:a", "copy", str(fallback),
], check=True)
print("Synthetic 7-second 240p H.264/AAC and MPEG-4/AAC fixtures generated locally", flush=True)
subprocess.run([candidate, fixtures, str(media), str(fallback)], check=True)
