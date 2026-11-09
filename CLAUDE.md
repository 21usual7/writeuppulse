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
After a change the user should see, copy `build/writeuppulse` to `~/.local/bin/`. If
switching to `bin/install`, remove those copies first or they shadow the package.

## Architecture

One binary, two modes (`src/main.cpp`): any first argument other than `open` goes to
`runCli` (`src/cli.cpp`, headless `QCoreApplication`: `fetch`, `list`, `show`,
`export`, `publish`, `skill install`, all with `--json`); otherwise the QML window.

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
- The harness must run with its own `XDG_CONFIG_HOME`: keys like U or the language
  pills otherwise write into the user's real `~/.config/writeuppulse`.
- A real `writeuppulse fetch` spends the user's Claude usage and takes minutes; don't
  run it just to test — use the fake `claude` script pattern in the tests.

## Conventions

- Commit messages in **English**, short and imperative ("Pick the language of …").
- Branch `master`, remote `git@github.com:21usual7/writeuppulse.git` (public). Push only
  when asked: `GIT_SSH_COMMAND="ssh -i ~/keys/git_key -o IdentitiesOnly=yes" git push`.
- README screenshots (`docs/screenshot.png`, `docs/focus.png`) use made-up data, the
  English UI, and the app's own palette; retake them when the UI changes.
