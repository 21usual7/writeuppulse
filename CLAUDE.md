# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

WriteupPulse is an Omarchy desktop app (C++17 + Qt 6 Quick, qmake, no CMake) built
following the `omarchy-app` skill. Every morning it runs Claude Code headless to find
5 popular technical writeups, summarize them, and save them; the window shows them.

## Commands

```sh
bin/build                      # qmake6 + make → build/writeuppulse
bin/test                       # builds and runs the Qt Test binary offscreen
QT_QPA_PLATFORM=offscreen build/tests/writeuppulse_tests fetcherReportsAgentErrors   # one test
build/writeuppulse             # open the window (same as `writeuppulse open`)
WRITEUPPULSE_DATA=/tmp/x build/writeuppulse   # run against a scratch data dir
bin/install                    # makepkg -fsi + enable the user timer (needs sudo)
```

On this machine the app is currently installed **without** the package, for the user
only: `~/.local/bin/writeuppulse`, `~/.local/share/applications/writeuppulse.desktop`,
`~/.config/systemd/user/writeuppulse.{service,timer}` (ExecStart uses `%h/.local/bin`).
After a change the user should see, copy `build/writeuppulse` to `~/.local/bin/` (to a
temp name, then `mv`, since the window may be running). If switching to `bin/install`,
remove those copies first or they shadow the package.

This machine is the publisher: its `~/.config/writeuppulse/writeuppulse.conf` has
`source=claude`, its local unit runs `writeuppulse fetch`, and the drop-in
`~/.config/systemd/user/writeuppulse.service.d/publish.conf` runs `bin/publish` after each
fetch with `GIT_SSH_COMMAND` set to `~/keys/git_key` and `WRITEUPPULSE_BIN` to the same
binary. `ExecStartPost=-` ignores a failed publish; `timeout 5m` and ssh's
`BatchMode`/`ConnectTimeout` keep a hung one inside the unit's 40-min `TimeoutStartSec`
(the fetch takes up to 30), because a unit timeout means `Restart=on-failure` reruns the
agent. `bin/publish` pushes whenever `pages` is ahead of `origin/pages`, so a push that
failed one day goes out the next, and it stops with an error if `pages` diverged. Site: <https://21usual7.github.io/writeuppulse/>,
served by GitHub Pages from the `pages` branch (only `bin/publish` writes it, from the
worktree in `~/.cache/writeuppulse/pages`). Changes to `site/` reach the site only once
committed on `master` and published.

## Architecture

One binary, two modes (`src/main.cpp`): any first argument other than `open` goes to
`runCli` (`src/cli.cpp`, headless `QCoreApplication`: `fetch`, `list`, `show`,
`update`, `sync`, `export`, `publish`, `skill install`, all with `--json`); otherwise
the QML window.

- **Fetch path:** `Fetcher` (`src/fetcher.cpp`) runs `claude -p <prompt>` with only
  `WebSearch,WebFetch` tools, `--strict-mcp-config`, `--output-format json` and a
  `--json-schema`; it reads `structured_output` from the result. The agent never writes
  files — `Store::normalize` validates its output (drops items without title/summary or
  with non-http(s) URLs) and `Store` saves `<date>.json` + `<date>.md` and `status.json`
  atomically (`QSaveFile`) in `~/.local/share/writeuppulse`. A `QLockFile`
  (`fetch.lock`) prevents concurrent runs. URLs from the last 7 days go into the prompt
  to avoid repeats. `findClaude()` also searches mise/`~/.local/bin` paths because the
  systemd unit runs with a bare PATH.
- **Window ↔ fetch decoupling:** the GUI never runs the agent itself. "Refresh now"
  does `systemctl --user start --no-block writeuppulse.service` (falls back to a
  detached `writeuppulse fetch`). `Backend` watches the data directory with
  `QFileSystemWatcher`; "running" = the lock file's pid is alive (read via
  `getLockInfo`, never `tryLock`, which would touch the directory and re-trigger the
  watcher), plus a short `m_starting` grace timer.
- **Progress:** `Progress` (`src/progress.cpp`) keeps what the user did — read,
  done in lab, favorite, tags, checked practice items — in `progress.json`, keyed by
  article URL (so it survives a day being re-fetched). Only the window writes it; the
  fetch never does. Streak = consecutive days with a writeup marked read (explicitly,
  never automatically). QML reads it through `backend.state(url)` in bindings that also
  touch `backend.progressRevision`, which bumps on every change.
- **Agent output fields:** category (fixed list in `Store`, grouped into topics; unknown
  → Other), level, 5–8 takeaways, 3–5 questions with answers, 3–6 practice ideas. The
  app is for learning, not attacking: the prompt forbids code/commands/payloads/PoCs,
  the schema has no code field, and `normalize` strips fenced code blocks. Old digests
  lack these fields and must still render.
- **Preferences and export:** the Settings popup (P) writes `categories` (empty = all)
  and `preferred` to `QSettings`; `Fetcher::prompt` reads them, and `Backend::rebuild`
  puts preferred directions first in a day. Export goes through `FilePicker`
  (abstract; tests inject a fake) → `PortalFilePicker`, adapted from Omacut, which
  asks xdg-desktop-portal's `SaveFile`; `Store::exportMarkdown` builds the text with
  the user's progress. `writeuppulse export` does the same headless.
- **Language:** one picker (en/ru/uk) stored in `QSettings("writeuppulse","writeuppulse")`
  drives both the UI (`Translator::setOverride`) and the summary language in the prompt
  (`Fetcher::language()`); unset → Omarchy's language if offered, else English.
  Catalogs are `src/i18n/<code>.json` keyed by the English `qsTr()` text; add new
  strings to both `ru.json` and `uk.json`.
- **Public site:** `site/` is a static page (vanilla JS, no build) that reads
  `data/index.json` and `data/<date>.json`. `Store::publish` writes those: every day
  re-run through `normalize` (so `costUsd` and progress never leave the machine) plus
  `topic`, and an index of every day already in the target, so it never drops days.
  `bin/publish` keeps a worktree of the `pages` branch in `~/.cache/writeuppulse/pages`,
  copies `site/` from committed `master`, runs `writeuppulse publish data`, commits, and
  pushes only `pages` (GitHub Pages serves it), so unpushed `master` work stays local.
  Agent text goes into the DOM only via text nodes; only http(s) URLs become links.
  Preview: `writeuppulse publish site/data` (ignored by git), then
  `python3 -m http.server -d site`.
- **Two sources:** QSettings `source` = `feed` (default) or `claude`. `update` (what
  the unit runs) dispatches to `sync` or `fetch`. `Syncer` (`src/syncer.cpp`) gets
  `index.json` and new or re-published days (by `generatedAt`) from the site with
  `QNetworkAccessManager` (size caps, transfer timeout), checks dates before they become
  file names, runs `normalize`, and saves through `Store` under the same lock and
  `status.json` as a fetch. In feed mode the window syncs on open and hourly if the last
  run is over 30 min old (only with `systemd` true, so tests never start anything).
  Tests use a `file://` feed. The maintainer's machine has `source=claude` and a local
  unit that runs `fetch` plus a drop-in (`writeuppulse.service.d/publish.conf`) that runs
  `bin/publish` after it.
- **Look:** the app has its own palette on purpose (the user chose it over following the
  Omarchy theme): colours, fonts, and radius live in the `Style` QML singleton
  (`src/Style.qml`, registered by `src/qmldir`). Layout is a reader — icon rail, list
  (`WriteupRow`), open writeup in tabs (`ReaderPane`) — plus `FocusView`, a step-by-step
  overlay. Controls are mostly hand-drawn (`Pill`, `ActionButton`); Material only
  styles fields, menus, and popups. `translator.{h,cpp}` comes from the omarchy-app
  templates. Keep agent-supplied text in QML as `textFormat: Text.PlainText`.

## Gotchas

- **moc silently skips the test file** ("No relevant classes found" → vtable link
  errors) when a test contains raw string literals with JSON like `R"({... "https://..."})"`.
  Build JSON fixtures with `QJsonObject` instead. After such a failure, `rm -rf build/tests`.
- Tests call `QStandardPaths::setTestModeEnabled(true)` in `initTestCase` so `QSettings`
  doesn't touch the real `~/.config`; keep it there.
- Never use `focus` as a QML id: every Item has a `focus` property, which wins, so
  `focus.x` silently reads `undefined`.
- There is no screenshot harness in the repo: write a small Qt program that loads
  `qrc:/Main.qml` with a `Backend` on `WRITEUPPULSE_DATA` (made-up digests), sends
  keys, and saves `grabWindow()`. Set `QT_FORCE_STDERR_LOGGING=1` to see QML warnings.
- Offscreen rendering (`QT_QPA_PLATFORM=offscreen`) is fine for the hand-drawn UI but
  does not draw Material control backgrounds (menus, switches); use
  `QT_QPA_PLATFORM=wayland` when those must look real.
- `build/writeuppulse` (window) talks to the real systemd units. Starting
  `writeuppulse.service` on this machine runs the agent (Claude usage) and then
  `bin/publish`, which pushes to the public site. It starts on open with `source=feed`,
  on R or Refresh now, and when Settings switches "Writeups from" to the site; the timer
  switch enables or disables the real timer. For scratch runs set `XDG_CONFIG_HOME` to a
  dir whose `writeuppulse/writeuppulse.conf` has `source=claude`, and touch none of
  those; `WRITEUPPULSE_DATA` doesn't reach the unit. `writeuppulse sync` against the live site into a
  scratch `WRITEUPPULSE_DATA` is safe (read-only, seconds).
- The harness must run with its own `XDG_CONFIG_HOME`: keys like U or the language
  pills otherwise write into the user's real `~/.config/writeuppulse`.
- A real `writeuppulse fetch` spends the user's Claude usage and takes minutes; don't
  run it just to test — use the fake `claude` script pattern in the tests.

## Conventions

- Commit messages in **English**, short and imperative ("Pick the language of …").
- Branch `master`, remote `git@github.com:21usual7/writeuppulse.git` (public). Push only
  when asked: `GIT_SSH_COMMAND="ssh -i ~/keys/git_key -o IdentitiesOnly=yes" git push`.
- Releases: bump `pkgver` in `pkgbuild/PKGBUILD` and `VERSION` in `writeuppulse.pro`
  (it becomes `APP_VERSION` for `setApplicationVersion`), `cd pkgbuild && makepkg -f` (not
  `bin/install`, which also installs), commit, push, then
  `gh release create vX.Y.Z pkgbuild/writeuppulse-X.Y.Z-1-x86_64.pkg.tar.zst --target master`
  (only the main package, not `-debug`). The README's install steps link to the latest
  release. Ask before publishing a release.
- README screenshots (`docs/screenshot.png`, `docs/focus.png`) use made-up data, the
  English UI, and the app's own palette; retake them when the UI changes.
