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

Status: building; the `.app`/`.dmg` packaging (`tools/macos/package_macos.sh`) is next. See the project plan.
