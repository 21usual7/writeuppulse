# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

WriteupPulse is an Omarchy desktop app (C++17 + Qt 6 Quick, qmake, no CMake) built
following the `omarchy-app` skill. Every morning Claude Code (headless, on the
maintainer's machine) finds 5 popular technical writeups and summarizes them; they are
published to a static site, and every other install downloads them from there. In the
window, an optional local Ollama model tutors the reader. Three roles, kept apart: Claude
searches and summarizes, the site distributes, Ollama only teaches.

The plan for growing it into a learning platform (audit, target architecture, stages,
what is done) is `docs/ROADMAP.md`; mark stages done there. **The Claude budget must not
grow** (the user's decision): keep `Fetcher`'s prompt and schema the size they are, and
build new features locally — SQLite, the local Ollama model, the article pages themselves.

## Status (2026-11-10)

Roadmap stages done, each with tests (47 passing) and docs:

- **0 (bugs):** start limits in `[Unit]` (packaged and local unit), statistics take
  the category from the day, progress keyed by `Store::canonicalUrl`.
- **1 (self-check):** "I knew it" / "I didn't" on revealed answers (focus mode and the
  Check yourself tab), kept as `events`, counted in Statistics and the export.
- **2 (data foundation):** `Database` (SQLite, migrations, version 2), `progress.json`
  imported, FTS5 search with BM25.
- **3 (article texts):** `Archive` saves each writeup's page as passages; search
  covers them; Settings switch "Save article texts for search".
- **4 (concepts):** `LlmClient` shared by `Tutor` and `Extractor`; the local model
  lists concepts, the app relates the ones named in the same passage (database
  version 3).
- **5 (graph view):** the Concepts view (G, rail ◈): map, search, inspector with
  the timeline of writeups and neighbours with their passages.

After stage 5 the user changed the plan (2026-11-10): the local model is too slow for a
tutor, so stage 6 (tutor modes) is dropped; stages 4–5 stay, extraction spread over 2 h.
New plan, in order, each done → noted here: **review** (spaced repetition, done),
**open references** (MITRE ATT&CK, CWE, CAPEC, OWASP Top 10 as JSON in the repo:
glossary for beginners, a model-free concept graph; done), **level tracks** (my level in
Settings, a Junior and a Senior pick a day, security first; done), **open labs** (curated
category/CWE → Juice Shop, WebGoat, DVWA, pwn.college…; done), **notes** (per writeup, export
to an Obsidian vault / Markdown folder and Notion; done), **Help and Get started** (done), then
the same review/glossary/levels/RSS on the **site** (done).

On this machine `~/.local/bin/writeuppulse` is the build from the end of 2026-11-10
(references, review, notes, site publishing with terms/labs/feed); the database moves to
version 4 the first time a window from it opens. Everything through this list is pushed to
`master` (2026-11-10); the last release is still 0.3.0, so cutting 0.4.0 would need the
user's go-ahead. The morning run publishes
`site/` from committed `master` with this binary, so the site gets the new features then.

Later the same day: notes moved to their own window (N), line icons (`Icon.qml`), a
softer palette, thin scroll bars, and the settings scroll bar no longer covers the
fields. README screenshots retaken except `docs/focus.png` (old palette and icons;
its made-up data isn't in the repo).

**Next:** nothing planned beyond the list above; see `docs/ROADMAP.md`. Open ideas:
retake `docs/focus.png`. Sync between computers is done (2026-11-10, see Cloud sync;
`docs/ROADMAP.md` §11); not yet: syncing on window close, the Notion page id, and
rate limits on the Worker beyond its per-account cap. GPU: the drop-in `/etc/systemd/system/ollama.service.d/igpu.conf`
(`OLLAMA_IGPU_ENABLE=1`) exists since 2026-11-10 12:46 but needs `daemon-reload` and a
restart of `ollama` to apply; then measure tokens/s against the CPU's ~6.

## Commands

```sh
bin/build                      # qmake6 + make → build/writeuppulse
bin/test                       # builds and runs the Qt Test binary offscreen
QT_QPA_PLATFORM=offscreen build/tests/writeuppulse_tests fetcherReportsAgentErrors   # one test
build/writeuppulse             # open the window (same as `writeuppulse open`)
WRITEUPPULSE_DATA=/tmp/x build/writeuppulse   # run against a scratch data dir
bin/install                    # makepkg -fsi + enable the user timer (needs sudo)
WRITEUPPULSE_DATA=/tmp/x build/writeuppulse sync   # download from the live site (safe)
bin/publish                    # push the saved days to the site (pages branch only)
sqlite3 ~/.local/share/writeuppulse/library/library.db   # inspect progress and the index
```

On this machine the app is currently installed **without** the package, for the user
only: `~/.local/bin/writeuppulse`, `~/.local/share/applications/writeuppulse.desktop`,
`~/.config/systemd/user/writeuppulse.{service,timer}` (ExecStart uses `%h/.local/bin`).
After a change the user should see, copy `build/writeuppulse` to `~/.local/bin/` (to a
temp name, then `mv`, since the window may be running). If switching to `bin/install`,
remove those copies first or they shadow the package. A window started from the old
binary keeps running it: after a change to storage, ask the user to reopen the window.
Here `progress.json` was imported into the database on 2026-11-09 and is now only a
stale backup; don't read it as the current progress.

This machine is the publisher: its `~/.config/writeuppulse/writeuppulse.conf` has
`source=claude`, its local unit runs `writeuppulse fetch`, and the drop-in
`~/.config/systemd/user/writeuppulse.service.d/publish.conf` runs `bin/publish` after each
fetch with `GIT_SSH_COMMAND` set to `~/keys/git_key` and `WRITEUPPULSE_BIN` to the same
binary. `ExecStartPost=-` ignores a failed publish; `timeout 5m` and ssh's
`BatchMode`/`ConnectTimeout` keep a hung one inside the unit's 40-min `TimeoutStartSec`
(the fetch takes up to 30), because a unit timeout means `Restart=on-failure` reruns the
agent. `bin/publish` pushes whenever `pages` is ahead of `origin/pages`, so a push that
failed one day goes out the next, and it stops with an error if `pages` diverged.
Site: <https://21usual7.github.io/writeuppulse/>, served by GitHub Pages from the `pages` branch (only `bin/publish` writes it, from the
worktree in `~/.cache/writeuppulse/pages`). Changes to `site/` reach the site only once
committed on `master` and published.

Ollama (package `ollama` + `ollama-vulkan` for the Radeon 680M, which Ollama 0.40 still
skips as an integrated GPU unless `OLLAMA_IGPU_ENABLE=1` is set, so it runs on CPU) runs only on AC power, as
the user asked: `/etc/systemd/system/ollama.service.d/ac-power.conf` sets
`ConditionACPower=true`, and `/etc/udev/rules.d/99-ollama-ac-power.rules` runs
`/usr/local/bin/ollama-ac-power` through `systemd-run --on-active=3
--timer-property=AccuracySec=1s` on each `Mains` change: it checks `systemd-ac-power`
and stops or starts Ollama (only while the service is enabled), logging its decision
to the journal. The 1 s accuracy matters: by default a transient timer may fire up to a
minute late and then read the wrong state. Don't run models on battery for tests.

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
- **Window ↔ fetch decoupling:** the GUI never runs the agent or a sync itself. "Refresh
  now" does `systemctl --user start --no-block writeuppulse.service` (falls back to a
  detached `writeuppulse update`). `Backend` watches the data directory with
  `QFileSystemWatcher`; "running" = the lock file's pid is alive (read via
  `getLockInfo`, never `tryLock`, which would touch the directory and re-trigger the
  watcher), plus a short `m_starting` grace timer.
- **Database:** `Database` (`src/database.cpp`) is a SQLite file (`QT += sql`, the
  `qsqlite` driver, FTS5) at `<data>/library/library.db`. It is in a subdirectory on
  purpose: SQLite's journal would otherwise wake the data-directory watcher. Schema
  changes are migrations in `Database::migrate`, one `PRAGMA user_version` step per
  version inside `BEGIN IMMEDIATE`; never edit an applied step, add the next one. It
  holds user data nothing can rebuild (`progress`, `events`) and an index that
  `Database::index` rebuilds per day from the JSON files by mtime+size (`days`,
  `writeups` FTS5). JSON days stay the source of truth for content (site, sync).
  Only the window and `writeuppulse export` open it; fetch and sync never do.
  Version 1 imported `progress.json` once (canonical URLs) and left the file alone.
- **Progress:** `Progress` (`src/progress.cpp`) keeps what the user did — read,
  done in lab, favorite, tags, checked practice items, self-check answers — through
  `Database`, keyed by `Store::canonicalUrl` (http→https, no `www.`, fragment,
  `utm_*`/`fbclid`/`gclid`, or trailing slash), so it survives a day being re-fetched
  and links with different tails. Self-check answers are `events` rows (`kind =
  'check'`, subject = the question text, result `knew`/`missed`/empty to take it
  back); the entry's `checks` map is the latest per question. Streak = consecutive
  days with a writeup marked read (explicitly, never automatically). Statistics take
  each writeup's category from its day (`Backend::m_categories`), not the copy saved
  with the progress. QML reads it through `backend.state(url)` in bindings that also
  touch `backend.progressRevision`, which bumps on every change. Another window's
  commits are seen through a watch on the database file plus `PRAGMA data_version`.
- **Article pages:** `Archive` (`src/archive.cpp`, owned by `Backend`) downloads each
  writeup's own page in the window, one at a time, newest first, when the library
  changes (only with `systemd` true and QSettings `saveTexts`, default on). Links are
  untrusted (agent or feed): http(s) only; the host is resolved with `QHostInfo` and
  every address must pass `Archive::isPublic` (no loopback, private, link-local, ULA,
  CGNAT, multicast; IPv4-mapped judged as IPv4); redirects are followed by hand
  (`ManualRedirectPolicy`, raw `Location`, max 5) and checked again; 4 MB cap, 45 s
  inactivity timeout. Residual risk: the request resolves the name again (DNS
  rebinding); acceptable for a GET with no cookies, don't add credentials to it.
  `Archive::text` is regex HTML → text (prefers `<main>`/`<article>`, drops scripts,
  nav, header/footer/aside and `<pre>` code). Text goes into `passages` (FTS5, ~1000
  chars each) keyed by canonical URL, status into `pages` (`ok`, `skipped` for
  non-HTML, `refused`, `failed`, retried after 6 h up to 3 times). Tests set
  `allowPrivate` and serve pages from a `QTcpServer`. Claude is never involved.
- **Search:** each word is a quoted FTS5 prefix (`"word"*`, so typed operators are
  text) via `Database::match`, or part of one of the user's tags, or in the saved page
  (`Database::matchPages`, half weight); every word must match; results are ordered by
  summed BM25 (title 10, summary 4, rest 1). `bm25()` can't be used inside an
  aggregate, so per-page minima are taken in C++.
- **Agent output fields:** category (fixed list in `Store`, grouped into topics; unknown
  → Other), level, 5–8 takeaways, 3–5 questions with answers, 3–6 practice ideas. The
  app is for learning, not attacking: the prompt forbids code/commands/payloads/PoCs,
  the schema has no code field, and `normalize` strips fenced code blocks. Old digests
  lack these fields and must still render.
- **Review:** `Progress::review(library, today, newPerDay = 5)` is computed from the
  `events` history (no table of its own): every answer pushes a Leitner state, an empty
  result pops one; box 1–5, back after 1/3/7/16/35 days from the last answer, "missed"
  → box 1. New questions (never answered) join up to 5 a day, counting the ones first
  answered today, from writeups marked read, then earlier days' (never today's unread).
  `Backend::review` (property on `progressChanged`) feeds `ReviewView.qml` (V, rail ↻
  with a badge; Space reveals, Y knew, N didn't), which shows the first card: answering
  through `setCheck` drops it from today's list. Answers from focus mode and the Check
  yourself tab feed the same history. The streak counts review days too.
- **References:** `bin/references` (Python, stdlib) downloads MITRE ATT&CK Enterprise
  (STIX), CWE, CAPEC and writes `src/references/references.json` (~1.1 MB, in the qrc;
  commit it, the app never downloads it): per entry id (`T1059.001`, `CWE-79`,
  `CAPEC-63`, `A01:2025`), kind (`technique`, `weakness`, `pattern`, `owasp`), a
  readable name (CWE's common name when its own is long; the official one in `title`),
  a ~400-char description, the official link, match `terms`, and the catalogs'
  `related` ids (CWE parent, CWE↔CAPEC, CAPEC→ATT&CK, OWASP 2025 grouping→CWE from CWE
  view 1450, sub-technique→technique). A term belongs to one entry (ATT&CK, then CWE,
  then CAPEC). `GENERIC` drops names that mean something else in most texts (PHP, ISN,
  Windows, MFA…); tune it against real pages (`passages` of the user's db) when adding
  catalogs. `References` (`src/references.cpp`, a singleton read from the qrc) matches
  whole words, longest term first; single-word abbreviations (XSS, AiTM) only in their
  capitals; apostrophes dropped ("isn't" ≠ ISN). Note "Stored XSS" is CAPEC-592, not
  CWE-79: longest match wins. `Backend::scan` caches per canonical URL (title + summary
  + takeaways as text 0, then each passage), dropped on `Archive::saved` and library
  reloads. `Backend::references(url)` feeds "Terms in this writeup" under the summary
  (definition, link, "Show on the map"). `graphSource` (QSettings, default
  `references`) switches `graph`/`conceptInfo` between `referenceGraph`/`referenceInfo`
  (same shapes plus `text`, `link`, `title`, `catalog`) and the model's tables.
  Attribution text is in the JSON's `notice`, the README, and the map's footer: keep it.
- **Levels:** QSettings `myLevel` (Junior/Middle/Senior or empty) via `Backend::myLevel`:
  `rebuild` ranks a day's items by my level (2) + preferred direction (1), stable;
  `Backend::review` puts my level's writeups first so new questions come from them;
  `WriteupRow` marks "your level". The prompt asks for security directions first (3 of
  5 when they're wanted) and at least one Junior and one Senior a day; its length was
  kept by shortening the source list (budget rule).
- **Notes:** `notes` table (database version 4: url, text, notion page id). Notes open
  in `NotesWindow.qml`, a separate `Window` (N, or "Notes" among the reader's actions;
  the user asked for a mini window, one at a time): it stays on the writeup it was
  opened for while the main window moves on, and `openFor` another writeup saves and
  switches. It saves 0.8 s after typing stops, when it loses focus, and on close; the
  main window closes it. The main window's shortcuts are `Qt.ApplicationShortcut`, so
  they fire in that window too: `win.typing` counts `notesWindow.active`, and Esc in
  Main is off while it's active. `Notes` (`src/notes.cpp`): `markdown()` = YAML front matter
  (title, url, date, category, level, tags incl. `writeuppulse` and the user's) +
  summary, takeaways, catalog terms as `[[ID Name]]` wiki links, "My notes";
  `writeToVault(folder, …)` writes `<folder>/WriteupPulse/<date> <title>.md` and keeps a
  hash of what it wrote in `<folder>/WriteupPulse/.writeuppulse` (QSettings ini, key =
  hash of the path): a file changed since (or not written by the app) is never
  overwritten. With QSettings `notesFolder` set, every note save writes it; "Save to
  notes folder" does it on demand. `sendToNotion` (`Notion-Version: 2022-06-28`,
  `WRITEUPPULSE_NOTION` overrides the API base for tests) archives the previous page
  (PATCH `archived`) and POSTs a new one under `notionParent` (`notionId()` takes a page
  link or ID): bookmark, headings, paragraphs (2000-char pieces), ≤ 100 blocks. The
  token is in QSettings (plain, ~/.config), as the Settings text says; only the user's
  own action sends anything.
- **Cloud sync:** `CloudSync` (`src/cloudsync.cpp`, owned by `Backend`) and the service
  `cloud/worker.js` (Cloudflare Worker + D1 `writeuppulse-sync`, deployed 2026-11-10 at
  <https://writeuppulse-sync.21usual7.workers.dev>, the default; `WRITEUPPULSE_CLOUD`
  overrides it). Key `wp1-` + 32 random bytes base64url, in QSettings `cloudKey` (plain,
  like the Notion token) with the pull cursor `cloudCursor`. From it, by HMAC-SHA256
  labels: the bearer token (the Worker files rows under its SHA-256), the row id key
  (id = HMAC(kind + "\n" + key)), and the AES-256-GCM key (OpenSSL `-lcrypto`; nonce |
  ciphertext | tag). The service never sees a URL, note, or question. Locally (database
  version 5) `sync` (kind `progress`/`note`/`event`, key, changed ms, sent) is touched
  by `saveEntry`, `saveNote`, `addCheck`; events got a random `uid`; `applyRemote`
  takes a row only if newer than ours (last write wins; answers are only added) and
  checks are ordered by `at, id` so another computer's fall in place. `shareAll` marks
  everything on joining (time 0, so any real change wins). One `POST /sync` sends ≤ 400
  rows and returns rows after the cursor (≤ 1000, `more`). The window syncs 1.5 s after
  open, then each minute if something is pending or 10 min passed; a received row
  reloads progress like another window's commit. Worker limits: 4 MB body, 500 rows
  in, 64 KB per row, 50 000 rows per account. Deploy: `cd cloud && npx wrangler@4
  deploy` (wrangler is logged in on this machine; schema changes go to `schema.sql`
  and `d1 execute --remote`). The test `cloudSyncKeepsComputersTheSame` uses a stand-in
  with the Worker's rules; `WRITEUPPULSE_CLOUD_TEST=<url>` runs it against `wrangler dev
  --local` or the real one (then delete its rows).
- **Labs:** `src/labs/labs.json` (qrc) is a hand-picked list of legal practice places
  (name, own project URL, `open` = open source and self-hosted vs free online,
  directions, levels, `about` in en/ru/uk). `Backend::labs(category, level)` gives up to
  three, this level's first, then open source; the Practice tab lists them under the
  practice ideas. Check every URL responds and every claim is true before adding one;
  only legal, self-owned or purpose-built targets.
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
  `publish` also adds each item's `terms` (`Store::terms`: catalog entries its title,
  why, summary, takeaways name, ≤ 8, with definition and link), writes `labs.json` (the
  qrc copy) and `feed.xml` (Atom, newest 30, UTC times). The site has a level picker
  (sorts, marks "your level"), Terms and "Where to practice legally" on a writeup, "I
  knew it / I didn't" under each answer, and `#/review` (same Leitner rules as the app,
  over the last 30 days, new questions from opened writeups first); level, opened
  writeups (`wp.seen`), and answers (`wp.cards`) live in the browser's localStorage only.
  The site must still work with older data (no `terms`, no `labs.json`): it does.
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
  Tests use a `file://` feed. The packaged unit runs `update`; this machine's runs
  `fetch` (see the publisher setup above).
- **Local model:** `LlmClient` (`src/llmclient.cpp`, created in `main` and shared) is
  the only Ollama HTTP client (`WRITEUPPULSE_OLLAMA`, default `http://127.0.0.1:11434`;
  model in QSettings `tutorModel`, default `qwen3:8b`). `status` comes from `/api/tags`:
  `ready`, `noModel`, `missing`, or `battery` when Ollama is unreachable and
  `onAcPower()` (reads `/sys/class/power_supply`) is false (this machine stops Ollama
  off AC, see above). `chat` streams `/api/chat` (NDJSON, `think: false`, `num_ctx`
  8192 for every chat so the model isn't reloaded between users), optionally with a
  JSON schema as `format`; `take()` parses the stream. A foreground chat emits
  `foregroundStarted`, and `idle()` is false until it ends.
- **Tutor:** `Tutor` (`src/tutor.cpp`, QML context property `tutor`) forwards
  `status`/`model`/`check` to the `LlmClient`. `ask` streams with a system prompt
  holding the writeup and the no-code rule, plus a practice idea when one is open.
  Conversations are in memory, keyed by URL + practice index (−1 = whole writeup),
  capped at 20 messages. `clean()` cuts `<think>` and fenced code. UI: the Ask tab
  (`TutorChat.qml`, key 5) and "Discuss" on `PracticeStep`. Tests use a `QTcpServer`
  stand-in for Ollama. Claude still does all searching and summarizing; the tutor
  never writes to disk.
- **Concepts:** `Extractor` (`src/extractor.cpp`, owned by `Backend` once `main` calls
  `setLlm`) asks the local model, never Claude, only for the concepts a writeup teaches
  (≤ 20, English names, ≤ 3 aliases, kind: concept/technique/tool/vulnerability/
  technology), held to `Extractor::schema()` as `format`; the `maxItems` limits and
  `num_predict` 1500 are what stop qwen3:8b from listing variations of one name until
  it runs out of tokens (seen on real pages). Relations come from the app, not the
  model (qwen3:8b made stars around one concept with made-up passage numbers):
  `validate` relates concepts named, by name or alias as whole words, in the same
  passage of the whole saved page (`RELATED_TO`, first passage, `weight` = passages
  shared, the 30 strongest, ≤ 6 per concept; a passage naming more than 6 is an
  overview and relates nothing), and drops general words the model lists anyway (exploit, CVE,
  proof of concept). Material: the first ~12000 characters of `Database::passages`,
  or the summary and takeaways when there is no page (`source` `summary`, passages
  then −1). One writeup at a time, only while `LlmClient` is `ready`; with page
  downloads on it waits until the `Archive` has tried the page. A tutor question
  aborts the running extraction, which restarts `pauseMs` (60 s) after the last
  answer; a refused connection (Ollama stopped on unplugging) requeues without
  counting. A batch is spread over `spreadMs` (2 h, the user's choice: the model
  shouldn't hold the machine for an hour at a stretch): each writeup starts at least
  `spreadMs / batch size` after the previous one started. Nothing starts off AC power
  (`LlmClient::onAcPower(powerSupply)`), even where Ollama keeps running on battery,
  and a running extraction is stopped and requeued within `powerCheckMs` (60 s) of
  unplugging. Stored by `Database::saveConcepts` (version 3): `concepts` (id =
  `Extractor::id(name)`), `aliases` (every name and alias → the concept that first had
  it, so "XSS" in one writeup joins "Cross-site scripting" from another), `mentions`
  (url, concept), `edges` (url, source, target, type, passage, weight, model; `type`
  leaves room for typed relations later), `extractions` (status, source, model,
  attempts). Summary concepts are redone once the page is saved; failures retry after
  6 h up to 3 times, and a failure after a good run keeps the good one. On this
  machine's CPU a writeup takes ~2–4 min at ~6 tokens/s (Ollama 0.40 drops the 680M
  iGPU unless `OLLAMA_IGPU_ENABLE=1`).
- **Concept map:** `Database::graph(urls)` sums every writeup's edges per pair
  (either direction) and `Database::conceptInfo(id, urls)` gives one concept with
  aliases, models, and neighbours with evidence (passage text, empty for a summary);
  both take the library's canonical URLs, so concepts of removed days drop out.
  `Backend::graph` (property, `graphChanged` after each extraction, library change,
  or another window's commit) adds positions from `Backend::layout`
  (Fruchterman–Reingold from a sunflower start, deterministic, gravity so separate
  writeups stay on screen, scaled to a [0, 1] square) and is computed lazily: only
  `GraphView`, behind a `Loader` active in the `graph` view, reads it. Layout is
  O(n²·300) on the GUI thread (~0.1 s for 300 concepts, ~0.3 s for 600); move it off the thread
  if libraries grow to thousands. `Backend::conceptInfo` adds the library items
  (oldest first) and titles. `GraphView.qml` draws edges on a `Canvas` and nodes as
  items in a scaled `world` (drag pans, wheel zooms; sizes and labels divide by the
  scale); `win.openWriteup(item)` jumps to the writeup's day, clearing topic and level
  filters (and "unread only") if they hide it. It always says the concepts are the
  model's reading.
- **Help and Get started:** `HelpView.qml` (in the `?` popup) is the full help: a day's
  routine, the views, the tabs, the no-attack rule, what stays local, every shortcut;
  keep its shortcut table in step with `Main.qml`'s Shortcuts and the README.
  `GetStarted.qml` is a 4-step overlay (what it is; level and language; a day; sources
  and privacy) shown while QSettings `onboarded` is false, and again from Help ("Get
  started again", `win.startAgain`). While it shows, `win.typing` is true so the
  single-key shortcuts stay off. A scratch harness needs `onboarded=true` in its conf
  or every screenshot shows it.
- **Look:** the app has its own palette on purpose (the user chose it over following the
  Omarchy theme): colours, fonts, and radius live in the `Style` QML singleton
  (`src/Style.qml`, registered by `src/qmldir`). Layout is a reader — icon rail, list
  (`WriteupRow`), open writeup in tabs (`ReaderPane`) — plus `FocusView`, a step-by-step
  overlay. Controls are mostly hand-drawn (`Pill`, `ActionButton`); Material only
  styles fields, menus, and popups. Icons are `Icon.qml` (24×24 SVG paths drawn by
  `QtQuick.Shapes` with the curve renderer, coloured from `Style`; add a path to its
  map rather than a font glyph). Scroll bars are `Bar.qml` (thin, the app's colours;
  Material's grew into a wide block over the content). The palette (2026-11-10, the
  user found the old one harsh): soft slate, not black; `faint` text ≥ 4.5:1 on panes.
  `ReaderPane` resets the tab only when the writeup's URL changes: a rebuilt list
  hands over a new object for the same writeup. `translator.{h,cpp}` comes from the omarchy-app
  templates. Keep agent-supplied text in QML as `textFormat: Text.PlainText`.

## Gotchas

- `Database` objects own a named `QSqlDatabase` connection and remove it in the
  destructor; don't copy them or keep `QSqlQuery`/`QSqlDatabase` handles past them.
  In tests, declare `Database` before the `Progress` that uses it.
- **moc silently skips the test file** ("No relevant classes found" → vtable link
  errors) when a test contains raw string literals with JSON like `R"({... "https://..."})"`.
  Build JSON fixtures with `QJsonObject` instead. After such a failure, `rm -rf build/tests`.
- Tests call `QStandardPaths::setTestModeEnabled(true)` in `initTestCase` so `QSettings`
  doesn't touch the real `~/.config`; keep it there.
- `QJsonArray::last()` returns a copy, and chained `json["a"]["b"]` on refs can quietly
  read nothing: use `toObject().value(...)` and `QJsonArray::replace`.
- Never use `focus` as a QML id: every Item has a `focus` property, which wins, so
  `focus.x` silently reads `undefined`.
- There is no screenshot harness in the repo: write a small Qt program (in the scratchpad,
  its .pro with `QT += ... sql` listing the `src/*.cpp` it needs, `src/database.cpp`
  among them, and `src/resources.qrc`) that sets the
  `translator`, `backend`, and `tutor` context properties, loads `qrc:/Main.qml`, sends
  keys, and saves `grabWindow()`. Construct `Backend({}, false)` so it never syncs on open
  (R still starts a detached `update`). Point `WRITEUPPULSE_DATA` at made-up digests and
  `WRITEUPPULSE_OLLAMA` at a fake (a few lines of Python `http.server`: `/api/tags` plus an
  NDJSON `/api/chat`), or at the real Ollama for a real answer (qwen3:8b, CPU: ~20 s
  warm). Set `QT_FORCE_STDERR_LOGGING=1` to see QML warnings and `qInfo` output.
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
  (it becomes `APP_VERSION` for `setApplicationVersion`; make doesn't track DEFINES, so
  `rm build/main.o` first or the binary keeps the old version), `cd pkgbuild && makepkg -f` (not
  `bin/install`, which also installs), commit, push, then
  `gh release create vX.Y.Z pkgbuild/writeuppulse-X.Y.Z-1-x86_64.pkg.tar.zst --target master`
  (only the main package, not `-debug`). The README's install steps link to the latest
  release. Ask before publishing a release.
- README screenshots (`docs/screenshot.png`, `docs/focus.png`) use made-up data, the
  English UI, and the app's own palette; retake them when the UI changes.
