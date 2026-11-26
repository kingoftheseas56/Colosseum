# Colosseum stable updates from 1.5.0

Packaged builds check the existing stable feed after launch. Windows uses the signed v1 manifest and its in-app installer. Supported Android, Linux and macOS builds use the signed v2 manifest and open the matching package on the official GitHub release. The update page offers APK, AppImage and DEB, or DMG according to the platform.

## Prepare the release

1. Build the Windows installer and the four platform packages under the names in `scripts/update/generate_update_manifest.py`. Use the final distributable binaries for hashing. For the Android update, compare the candidate APK with the previous public APK using `aapt dump badging` and `apksigner verify --print-certs`. Check that the application ID and signing certificate match, the version name is correct, and the version code increases.
2. Generate both signed manifest pairs with the production update key.

   ```text
   python scripts/update/generate_update_manifest.py --version X.Y.Z --installer <exe> --platform-dir <packages> --presentation <release-presentation> --private-key <update-key> --output-dir <metadata>
   ```

3. Put the presentation artwork in the metadata directory. Use the same installer, packages, metadata directory and public key for the draft and its publication check.

   ```text
   python scripts/publish_app_release.py --draft --version X.Y.Z --installer <exe> --platform-dir <packages> --manifest-dir <metadata> --public-key-der <update-public-key-der>
   python scripts/publish_app_release.py --publish-verified-draft --version X.Y.Z --installer <exe> --platform-dir <packages> --manifest-dir <metadata> --public-key-der <update-public-key-der>
   ```

   The first command uploads the verified assets to the draft. The second compares the uploaded sizes and SHA-256 digests before publication. The stable feed follows the latest public GitHub release.
4. Check the production feed at `https://getcolosseum.world/updates/stable.json` after publication. Check the visible version and both signed manifest pairs.

## Qualify the first release

Before publishing 1.5.0, package a fixture build with `COLOSSEUM_UPDATE_TESTING=ON` and
`COLOSSEUM_UPDATE_TEST_FEED_URL` set to a staging HTTPS release envelope. Sign the 1.5.1 manifests
with the updater test key. In that envelope, use paths relative to the feed for the platform
packages, such as `downloads/v1.5.1/Colosseum-Android-1.5.1-arm64-v8a.apk`, exposed as full staging HTTPS URLs.
Build public candidates with the testing option off and the test feed empty.

Against the signed fixture, on Windows, check the in-app download and restart path. On Android, open the APK and confirm an in-place upgrade on the OnePlus and Nokia T20. On Linux, exercise both the AppImage and DEB downloads. On Apple Silicon, open the DMG and replace the app. Check update discovery on launch, the Home badge, and the absence of a new offer after upgrading. Run the same-version and invalid-signature cases against the test feed.
