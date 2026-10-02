# Android reader composition probe

A-01 Task Zero only. This standalone app hosts a real Android WebView as a
foreign QWindow and places a Qt Quick child window above it. It does not
implement EPUB reading or change the production reader.

Configure this directory with the Android kit's `qt-cmake`, specifying the
matching `QT_HOST_PATH`, `ANDROID_SDK_ROOT`, and `ANDROID_NDK_ROOT`. Build the
`apk` target and install its debug APK. Repeat with arm64-v8a and x86_64 kits.

Check that the paper renders; the overlapping button counts touches; uncovered
paper scrolls; text can be selected; the input accepts text and shows the IME;
rotation keeps both planes aligned; background/foreground preserves input;
and repeated Recreate WebView operations do not leave orphan views or crash.
Inspect `reader-probe` logcat messages alongside screenshots.

The approved architecture requires these checks on real hardware before the
production EPUB bridge is implemented. Emulator evidence alone does not close
that gate. HTML is inline, JavaScript and network/file/content access disabled.
