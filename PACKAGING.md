# Packaging a `.geode` release

A `.geode` file is a ZIP archive. Geode reads these entries from it:

| Entry | Required | Notes |
| --- | --- | --- |
| `mod.json` | yes | Mod ID, version, settings, resources, links |
| `logo.png` | recommended | Icon shown in-game |
| `about.md`, `changelog.md`, `support.md` | recommended | In-game mod page |
| `LICENSE` | no | Kept for attribution |
| `resources/` | no | Extra files declared in `mod.json` |
| `<mod id>.dll` | yes, for Windows | Windows binary |
| `<mod id>.dylib` | yes, for macOS | macOS binary |
| `<mod id>.ios.dylib` | yes, for iOS | iOS binary |
| `<mod id>.android32.so` / `<mod id>.android64.so` | yes, for Android | Android binaries |

The canonical file name for a release is the mod ID plus `.geode`:
`mxidevofc.click_before_frames.geode`. A versioned copy such as
`Click-Before-Frames-v1.5.1.geode` can be uploaded alongside it.

## Build the package

```bash
# metadata/assets only (no compiled binaries)
python3 tools/package_geode.py

# full, installable release: point at the output of `geode build` or the
# "Build Output" artifact from .github/workflows/multi-platform.yml
python3 tools/package_geode.py --binaries bin --strict --output mxidevofc.click_before_frames.geode

# optional human-readable, versioned copy for the release page
python3 tools/package_geode.py --binaries bin --strict --output Click-Before-Frames-v1.5.1.geode
```

`--binaries` may be repeated and accepts binaries named after the old ID
(`syzzi.click_between_frames.*`), the CMake target (`ClickBeforeFrames.*`), or
any file with a known platform suffix; the script renames them to the current
mod ID. `--strict` refuses to build a release without binaries. Every build
also writes a `<file>.geode.sha256` checksum sidecar.

## Getting the binaries

The repository's CI workflow builds Windows, macOS, iOS, Android32 and
Android64 and combines them into one `.geode` via
`geode-sdk/build-geode-mod/combine`. Locally you need the Geode SDK and the
matching platform toolchains:

```bash
geode build
# or, for Android
geode build -p android64
```

This sandbox has neither the Geode SDK nor the cross-compilers, and GitHub
release-asset hosts are unreachable here, so the `.geode` file generated in
this workspace contains the complete metadata/assets but **no platform
binaries** — it is a valid archive for inspection/distribution of the assets,
but Geode will not load it as a mod until the compiled binaries are added.

## Publishing

1. Add the binaries and re-run the script with `--strict`.
2. Verify the archive: `unzip -l mxidevofc.click_before_frames.geode`.
3. Attach the `.geode` and its `.sha256` to a GitHub Release tagged `v1.5.1`.
4. Optionally publish to the Geode index using the same `.geode` file.
