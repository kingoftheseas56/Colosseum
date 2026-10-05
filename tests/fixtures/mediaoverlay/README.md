# Original narrated EPUB fixture

Three original two-sentence chapters. Chapters 1 and 3 each have two one-second cues, nested sequences, and a separate two-second MP3 tone. Chapter 2 is deliberately un-narrated. Audio is synthetic 440/660 Hz, not spoken narration. It is real decodable audio, not a placeholder. The first ZIP entry is the uncompressed mimetype. tiny-narrated-é.epub is byte-identical and tests Unicode filesystem paths.

Regenerate audio with ffmpeg: `ffmpeg -f lavfi -i sine=frequency=440:duration=2 -c:a libmp3lame -b:a 64k -map_metadata -1 one.mp3` (660 Hz for three.mp3). Repack unpacked sources with mimetype first and ZIP_STORED, other entries ZIP_DEFLATED. Expected canonical paths start with OPS/. Total declared cue duration is 4000 ms; encoded MP3 containers include encoder delay/padding and can be slightly longer.
