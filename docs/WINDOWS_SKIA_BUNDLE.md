# Windows Skia Bundle Maintenance

## Why the bundle exists

A clean Skia source build on GitHub-hosted Windows runners was too slow and was
not reliable enough for release builds. The Windows release workflow therefore
downloads a prebuilt static Skia bundle and builds the application. A source
build remains the fallback when the configured bundle cannot be downloaded.

The Linux workflow currently builds Skia from source. Its approximately
20-minute end-to-end runtime is acceptable, so Linux does not currently use a
prebuilt Skia bundle.

## Complex-text release gate

The shared HarfBuzz/ICU/BiDi implementation is complete and Linux-validated,
but it must not be released for Windows against the historical active bundle.
Use this order for the next Windows validation:

1. Pull the implementation and documentation commits on the Windows machine.
2. Build a clean replacement bundle with the pinned revision and the complete
   library/data set documented below.
3. Build `mdviewer` and `mdviewer_tests` against that local bundle, run CTest,
   and complete the GUI fixture, print, PDF, DPI, and portable-directory checks.
4. Publish the bundle under a new bundle tag without replacing the active tag.
5. Change `ci/skia-bundle-version.txt` in a separate commit, push it, and run the
   Windows workflow manually against the downloaded bundle.
6. Create an application release tag only after that clean-runner validation is
   green.

Do not fold bundle creation, activation, and application release into one
unvalidated change. `skia-bundle-v1` is not suitable for complex-text releases.

## History of `skia-bundle-v1`

The first Windows bundle was built locally and uploaded manually. This is
confirmed by repository and GitHub metadata:

- the release asset uploader is `Danmoreng`, not `github-actions[bot]`;
- the asset was uploaded before `publish-skia-bundle.yml` was added;
- the `skia-bundle-v1` tag points to the earlier Windows release-workflow commit.

The bundle contains Skia milestone 148. It does not contain an exact revision
record, so its precise Skia commit cannot be proven after the fact. Do not use
`skia-bundle-v1` as provenance for a new bundle.

## Pinned revision

The authoritative source revision is stored in:

```text
ci/skia-revision.txt
```

Both `build.ps1` and `build.sh` fetch and check out that exact commit before a
source build. They write the following provenance files beside the static Skia
library:

```text
SKIA_REVISION
SKIA_MILESTONE
SKIA_GN_ARGS
```

Never update Skia by changing a local checkout alone. Update
`ci/skia-revision.txt` in its own reviewed commit and validate both platform
builds.

## Creating a new bundle locally on Windows

Use a Windows machine with Visual Studio C++ tools, Python, Git, sufficient disk
space, and the repository checked out at the intended revision.

Build the pinned PDF-, SVG-, ICU-, HarfBuzz-, SkShaper-, and SkUnicode-enabled
Release Skia libraries:

```powershell
.\build.ps1 -Clean -Configuration Release -SkiaOnly
```

Confirm provenance:

```powershell
Get-Content .\third_party\skia\out\Static\SKIA_REVISION
Get-Content .\third_party\skia\out\Static\SKIA_MILESTONE
Get-Content .\third_party\skia\out\Static\SKIA_GN_ARGS
Test-Path .\third_party\skia\out\Static\icudtl.dat
```

`SKIA_GN_ARGS` must contain `skia_use_icu=true`,
`skia_enable_skunicode=true`, `skia_use_harfbuzz=true`,
`skia_use_system_harfbuzz=false`, and `skia_enable_skshaper=true`.

Build and test the application against that exact local output:

```powershell
.\build.ps1 -SkipSkia -Configuration Release
cmake --build build --config Release --target mdviewer_tests
ctest --test-dir build -C Release --output-on-failure
```

Also run the GUI smoke test:

```powershell
.\build.ps1 -SkipSkia -Configuration Release -RunSmokeTest
```

Before publishing, manually verify:

- `test-docs/markdown-rendering-fixture.md`;
- `test-docs/bidi-complex-text.md` and complex-text runtime initialization;
- scrolling and rapid selection in a real Unicode document;
- local links and Back/Forward;
- local images and syntax-highlighted code blocks;
- PDF export and native printing;
- resize and per-monitor DPI behavior;
- normal shutdown.

## Bundle layout

Create a staging tree with exactly this shape:

```text
third_party/skia/
  LICENSE
  include/
  modules/
    svg/include/
    skresources/include/
    skshaper/include/
    skunicode/include/
  src/core/
  out/Static/
    skia.lib
    svg.lib
    skresources.lib
    skshaper.lib
    skunicode_core.lib
    skunicode_icu.lib
    harfbuzz.lib
    icu.lib
    expat.lib
    icudtl.dat
    SKIA_REVISION
    SKIA_MILESTONE
    SKIA_GN_ARGS
```

Example PowerShell staging commands:

```powershell
$stageRoot = "dist\skia-bundle"
$bundleSkia = Join-Path $stageRoot "third_party\skia"
$bundleOut = Join-Path $bundleSkia "out\Static"
$zipPath = "dist\skia-windows-x64-static.zip"

Remove-Item $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $bundleOut -Force | Out-Null
New-Item -ItemType Directory -Path "$bundleSkia\modules\svg" -Force | Out-Null
New-Item -ItemType Directory -Path "$bundleSkia\modules\skresources" -Force | Out-Null
New-Item -ItemType Directory -Path "$bundleSkia\modules\skshaper" -Force | Out-Null
New-Item -ItemType Directory -Path "$bundleSkia\modules\skunicode" -Force | Out-Null
New-Item -ItemType Directory -Path "$bundleSkia\src" -Force | Out-Null
Copy-Item "third_party\skia\LICENSE" "$bundleSkia\LICENSE"
Copy-Item "third_party\skia\include" "$bundleSkia\include" -Recurse
Copy-Item "third_party\skia\modules\svg\include" "$bundleSkia\modules\svg\include" -Recurse
Copy-Item "third_party\skia\modules\skresources\include" "$bundleSkia\modules\skresources\include" -Recurse
Copy-Item "third_party\skia\modules\skshaper\include" "$bundleSkia\modules\skshaper\include" -Recurse
Copy-Item "third_party\skia\modules\skunicode\include" "$bundleSkia\modules\skunicode\include" -Recurse
Copy-Item "third_party\skia\src\core" "$bundleSkia\src\core" -Recurse
Copy-Item "third_party\skia\out\Static\skia.lib" "$bundleOut\skia.lib"
Copy-Item "third_party\skia\out\Static\svg.lib" "$bundleOut\svg.lib"
Copy-Item "third_party\skia\out\Static\skresources.lib" "$bundleOut\skresources.lib"
Copy-Item "third_party\skia\out\Static\skshaper.lib" "$bundleOut\skshaper.lib"
Copy-Item "third_party\skia\out\Static\skunicode_core.lib" "$bundleOut\skunicode_core.lib"
Copy-Item "third_party\skia\out\Static\skunicode_icu.lib" "$bundleOut\skunicode_icu.lib"
Copy-Item "third_party\skia\out\Static\harfbuzz.lib" "$bundleOut\harfbuzz.lib"
Copy-Item "third_party\skia\out\Static\icu.lib" "$bundleOut\icu.lib"
Copy-Item "third_party\skia\out\Static\expat.lib" "$bundleOut\expat.lib"
Copy-Item "third_party\skia\out\Static\icudtl.dat" "$bundleOut\icudtl.dat"
Copy-Item "third_party\skia\out\Static\SKIA_REVISION" "$bundleOut\SKIA_REVISION"
Copy-Item "third_party\skia\out\Static\SKIA_MILESTONE" "$bundleOut\SKIA_MILESTONE"
Copy-Item "third_party\skia\out\Static\SKIA_GN_ARGS" "$bundleOut\SKIA_GN_ARGS"
Compress-Archive -Path "$stageRoot\*" -DestinationPath $zipPath
Get-FileHash $zipPath -Algorithm SHA256
```

## Publishing without disrupting the active bundle

Publish a new tag such as `skia-bundle-v2`; do not overwrite the active bundle
while validating it:

```powershell
gh release create skia-bundle-v2 `
  .\dist\skia-windows-x64-static.zip `
  --title "Skia Bundle skia-bundle-v2" `
  --notes "Pinned Windows x64 PDF-, SVG-, ICU-, HarfBuzz-, SkShaper-, and SkUnicode-enabled Skia bundle."
```

The `Publish Skia Bundle` workflow can perform the same source build when
manually dispatched with a new `bundle_tag`, but local Windows creation remains
the known path when hosted-runner source builds are too slow.

Only after the bundle exists, change:

```text
ci/skia-bundle-version.txt
```

Do not change this file as part of the text-stack implementation itself. The
active bundle must stay pinned until the replacement bundle has been published,
tested with `mdviewer_tests`, and smoke-tested as a release archive. The Windows
workflow treats any bundle without the complete SkUnicode/HarfBuzz/ICU library
set and `icudtl.dat` as stale and falls back to a clean source build.

Then push that switch as a separate commit. Normal branch pushes intentionally
do not run GitHub builds, so start the Windows workflow manually to validate the
bundle on a clean runner before creating the next release tag. Do not combine
bundle publication and activation into a single unvalidated step.
