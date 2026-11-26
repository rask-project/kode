# Releasing Kode

## A release

1. Write the notes in `docs/releases/vX.Y.Z.md` (they open the GitHub
   Release; the changes since the previous stable tag follow).
2. Set `project(Kode VERSION X.Y.Z …)` in `CMakeLists.txt` and commit.
3. Tag the commit and push the tag:

   ```sh
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

`.github/workflows/release.yml` then builds Release on an Apple Silicon
runner, runs the tests, deploys Qt into the bundle (`package-macos`) and
publishes a GitHub Release with `Kode-X.Y.Z-arm64.dmg`, a `.zip` and
`SHA256SUMS.txt`. The bundle is signed ad hoc, not with a Developer ID, so it
is not notarized: users clear the quarantine flag once (README, "Install").

A tag with a suffix (`v1.1.0-beta.1`, `v1.1.0-rc.1`) publishes a
**pre-release** the same way; it is never marked Latest. The app shows the
tag as its version (`git describe`); `CFBundleVersion` orders pre-releases
before the release (`CMakeLists.txt`).

## The bundle

```sh
cmake -S . -B build/release -G Ninja \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/macos -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target package-macos
open build/release/dist/Kode.app
```

`package-macos` installs the self-contained bundle to
`build/release/dist/Kode.app`: Qt's deploy script (`qt_generate_deploy_qml_app_script`)
copies the Qt frameworks, plugins and QML modules, then
`cmake/PruneBundle.cmake` removes what Kode never loads (other Quick Controls
styles, QtWidgets, the TLS and network plugins) and signs it again. The
`build/release/Kode.app` of the build tree uses the Qt installation; never
distribute that one. Release builds also declare the document types and the
Finder Service (`KODE_FINDER_INTEGRATION`).

## Developer ID and notarization

With an Apple Developer ID, `-DKODE_CODESIGN_IDENTITY="Developer ID
Application: Name (TEAMID)"` signs the bundle with the hardened runtime and
a secure timestamp. Notarize it as Konsoll does (`konsoll/docs/RELEASE.md`,
steps 2–4): `xcrun notarytool submit` a `ditto` zip, `xcrun stapler staple`
the app, then make the final zip and disk image.
