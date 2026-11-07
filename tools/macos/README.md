# macOS (Apple Silicon) build

Built only on GitHub's Mac runners (`.github/workflows/macos.yml`): there is no Mac in the loop. Linux and Windows are built
by hand as before (`tools/release/make_release.sh`).

- The ROM is cloned from a private repository inside the job (secrets `ROM_REPO`, `ROM_REPO_TOKEN`) and never leaves the
  runner; the recompiled game code is made, compiled and discarded there. Without the secrets the job does nothing.
- macOS 15 or later, Apple Silicon (arm64), Metal through RT64.
- The program's pack and update lookups: texture and sample packs in the app's `Contents/Resources`
  (`platform::resources_dir`), settings, saves, ROMs and update notes in `~/Library/Application Support/AidynReChronicled`.
  The app bundle is never written to (it is ad-hoc signed); updates are the release page for now.
- `--version` prints the version and exits: the workflow's smoke test.

Status (2026-11-07): builds, packages (`tools/macos/package_macos.sh`) and runs on an M2 MacBook Pro (macOS 26.5.1). Fixed after the first
real run: RT64's Metal swap chain takes the view's `CAMetalLayer` (`SDL_Metal_GetLayer`), not the view; anti-aliasing is off by default
on a Mac (`settings.h`, and once in an existing settings.json). Logs from a Mac: `aidyn-mac-logs.sh` pushes them to a branch of the
private ROM repo. See the project plan.
