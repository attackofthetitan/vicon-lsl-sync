# v1.15.0 release checklist

## Release details

- Version: `1.15.0`
- Previous release: `v1.14.7`
- Target date: 2026-09-23
- Pull requests: none; released straight from `main`
- Status: ready; waiting for the tagged build to publish
- Scope: an easier Mac install, bundled recorders that work on any Mac, and a
  path check that looks at the study folder

A minor release. The Mac disk image now holds one app with LabRecorder inside it.
The app ejects its disk image, offers to move itself to Applications, and explains
the Mac folder permissions it asks for. The bundled recorders start on any Mac,
and the path check looks at the study folder instead of the folder the app was
started from.

## Before publishing

- [x] The CMake version and the dated changelog section both say `1.15.0`.
- [x] The full desktop build and all six test programs pass on an Apple Silicon
  Mac, including the window tests at normal and 1.5x scale and the new Mac
  install tests.
- [x] A package built on this Mac passes `.github/scripts/test-package-macos.sh`,
  with one local exception: Homebrew's Qt gives the bundled `libbrotlicommon` a
  fixed full path to where it was installed, which the Qt used by the release
  build does not. A v1.14.7 package built on this Mac fails the same check.
- [x] The packaged LabRecorder loads Qt and `lsl` from inside its own bundle, with
  the build folders hidden. The v1.14.7 package could not load `lsl` that way.
- [x] The app ejected a disk image of its own version, left other versions alone,
  tried again when the image was busy, and reported the result.
- [x] Moving the app copies it with a valid signature and without the "downloaded
  from the internet" flag, and reopening waits for the old copy to quit.
- [x] Generated stream files pass `tools/generate_stream_contracts.py --check`.
- [x] The release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` finds nothing.

## Publishing

- [ ] The release commit is pushed to `main` and its build passes before tagging.
- [ ] Tag `v1.15.0` points at the release commit.
- [ ] The tagged build passes the logic tests, HoloLens tests, and the full
  desktop build, tests, and packaging on Linux x64, Windows x64, and Apple
  Silicon Mac.
- [ ] Five platform downloads and `SHA256SUMS.txt` are published and checked.

## Not tested

- The Move to Applications prompt, the **Open Anyway** step, and the Mac folder
  and local network prompts need a Mac that has never run the app. Download the
  disk image, open the app from it, and record once.
- Real Unity/OpenXR/Vuforia, HoloLens, and Vicon tests were not run. Nothing
  about the devices changed.
- Windows and Linux builds, Windows packaging, and the HoloLens tests only run on
  the build server.
