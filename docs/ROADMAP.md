# WriteupPulse 2.0 — audit and roadmap

Stage 1 of the "Personal Technical Intelligence & Learning Platform" plan:
what the app is today, what is weak, and how to grow it **without raising the
Claude budget** (about $0.13 per daily run on 2026-11-09). Everything new runs
on the reader's machine: SQLite, the local Ollama model, and the article pages
themselves.

## 1. Current architecture

```
Claude (WebSearch/WebFetch) ──JSON schema──▶ Fetcher ──▶ Store::normalize ──▶ <date>.json/.md
                                                                     │
                   bin/publish ──▶ Store::publish ──▶ pages branch (GitHub Pages)
                                                                     │
                     other installs: Syncer ◀── index.json + <date>.json
Window: Backend (days, filters, search, systemd, export) + Progress (progress.json) + Tutor (Ollama)
```

What shapes every later decision:

- **No ingestion pipeline of our own.** Claude finds and reads the pages; the
  app only sees its structured answer. Full article text is never stored.
- **One publisher, many readers.** Feed readers never run Claude, so anything
  only Claude can produce has to travel in the published day format.
- **Small scale:** 5 writeups a day, ~1800 a year, a few MB. A graph database
  or a vector store is not justified.
- Storage is JSON files written atomically (`QSaveFile`); no schema versions,
  no migrations.

## 2. What already exists

| Feature | Where | State |
|---|---|---|
| Search and summary: category, level, takeaways, questions, practice | `fetcher.cpp`, `store.cpp` | works |
| Validation of agent output (http(s) only, code fences cut, known categories) | `Store::normalize` | works |
| Repeat avoidance: last 7 days' URLs go into the prompt | `Store::recentUrls` | exact string match only |
| Read, lab, favorite, tags, practice done, streak | `progress.cpp` | works |
| Substring search over all days, filters, preferred directions first | `Backend::rebuild` | works |
| Markdown export (day/week) | `Store::exportMarkdown`, portal | works |
| Public site and sync | `Store::publish`, `syncer.cpp`, `bin/publish` | works |
| Ollama tutor: chat about a writeup, practice reviews | `tutor.cpp`, `TutorChat.qml` | works, history in memory only |
| Self-check in focus mode | `FocusView.qml` | answers are revealed, **results are not recorded** |
| Statistics | `StatsView.qml` | read counts only |

Tests: 25 Qt Test cases, all passing; no QML tests.

## 3. Weaknesses and bugs

Bugs:

1. **The systemd restart limit doesn't apply.** `StartLimitIntervalSec` sits in
   `[Service]` (both `pkgbuild/writeuppulse.service` and the local unit);
   systemd logs `Unknown key 'StartLimitIntervalSec' in section [Service],
   ignoring`. It belongs in `[Unit]`. With a failure that persists, the agent
   reruns every 10 minutes without a limit, each time spending Claude usage.
2. **The category is copied into `progress.json`** when something is marked,
   so statistics keep a stale category after a day is re-fetched or re-synced
   (the real `progress.json` already holds `"category": "Other"`).
3. **Old days never gain the newer fields.** `2026-11-09.json` was written by
   an older binary and has no category, level, questions, or practice. Nothing
   upgrades such days.

Architecture:

- `Backend` (~450 lines) mixes days, filters, settings, systemd, export, and
  progress; `Main.qml` is 806 lines. New features must not land there.
- The day format has no version. `normalize` (run on publish and sync) drops
  every field it doesn't know: good for older apps, but each new field must be
  added to it explicitly or it never reaches the site.
- Progress is keyed by the raw URL: no canonical form (utm, trailing slash,
  `www`, http/https). Adding one later needs a migration.
- Prompts are hard-coded in `Fetcher` and `Tutor`; the language-name mapping
  is duplicated; the Ollama HTTP client lives inside `Tutor`.
- Level is only Junior/Middle/Senior; no prerequisites.
- Summaries follow the publisher's language, so concept ids must be canonical
  English for anything that merges across days.

## 4. Missing models

- **Document:** stable id (hash of the canonical URL), canonical URL,
  publication date and author when known, `schemaVersion`.
- **Passage:** a chunk of the article's own text, saved locally, for search,
  RAG, and evidence.
- **Concept:** id, name, aliases, kind (concept, technique, tool,
  vulnerability, technology).
- **Edge:** type (`REQUIRES`, `RELATED_TO`, `IMPLEMENTS`, `USES`, `EXPLOITS`,
  `EXTENDS`, `CONTRASTS_WITH`), provenance (passage it came from, or
  `inferred`), extractor (model name).
- **LearningEvent:** a self-check answer, teach-back, knowledge check, or
  practice review with its result. Knowledge state is derived from events,
  never stored as a "level".
- **Note:** the user's note, linked to a document and/or concepts.
- Also absent: persistent tutor history, tutor modes, retrieval, ranking.

## 5. Constraint: no extra Claude spending

The Claude request stays as it is (same schema, same prompt size). New work is
done locally:

| Need | Local way to get it |
|---|---|
| Source text for evidence and RAG | Download the article page itself (`QNetworkAccessManager`, http(s) only, size cap, timeout, text extracted from HTML, no scripts run), cached in SQLite, never published |
| Concepts, relations, prerequisites, difficulty factors | Ollama with a JSON schema in `format`, run over the saved text (or the summary when the page can't be fetched), incrementally, once per document |
| Same-subject grouping | Shared concepts + FTS5 similarity of titles and texts; exact duplicates by canonical URL |
| Full-text search | SQLite FTS5 (BM25) |
| Semantic search (optional) | Ollama `/api/embed` with a small embedding model, only when present |
| Tutor modes, checks, teach-back | Ollama, as today |

Consequences:

- Ollama runs only on AC power on this machine. Extraction is a queue that
  runs while the tutor is `ready` and resumes later; nothing blocks on it.
- Feed readers without Ollama get search and the library, but no graph. The
  window says so instead of showing an empty graph.
- `qwen3:8b` is a small model: extracted edges and grades are shown as
  "model-extracted"/"model's assessment", with the supporting passage, never
  as facts.
- Cross-source analysis (§3.3 of the brief) would need Claude to read extra
  sources; it is replaced by grouping writeups that share concepts.
- The no-code rule stays: exercises are in prose, like practice today.

## 6. Target architecture

JSON days stay canonical (the site and `Syncer` use them unchanged). A local
SQLite database (`QT += sql`; the `qsqlite` driver ships with qt6-base and
links the system sqlite 3.54, which has FTS5) holds:

- a **rebuildable index:** documents, passages, concepts, edges, FTS5 tables;
- **user data that can't be rebuilt:** learning events, notes, tutor
  history, progress (imported once from `progress.json`, which stays as a
  backup);
- migrations through `PRAGMA user_version`. Only the window writes.

Modules, one class per role like the current code:

- `Library`: database, migrations, indexing of days, FTS search, notes, page
  download. Search moves here from `Backend::rebuild`.
- `LlmClient`: the Ollama HTTP client taken out of `Tutor` (chat, structured
  output, embeddings), so a provider can be swapped in one place.
- `Extractor`: the background queue that turns a document into concepts and
  edges with a validated schema.
- `Knowledge`: graph queries (neighbours, prerequisite chains, timeline of
  a concept).
- `Learner`: events → state per concept (discovered, read, studied,
  practiced, demonstrated), weak areas, "why recommended".
- `Tutor`: modes as system prompts (Socratic, beginner, deep, teach-back,
  knowledge check, comparison); RAG passages cited as `[n]`.
- QML: Dashboard, Library, Graph (own force layout with Shapes, no new
  dependency), Learning; reached from the existing icon rail.

## Done

- 2026-11-09, stages 0–2: start limits moved to `[Unit]`; statistics take the
  category from the day; progress keyed by canonical URL; self-check answers
  recorded (focus mode and the Check yourself tab, counted in Statistics and the
  export); `Database` (the module called `Library` below) with migrations,
  `progress.json` import, and FTS5 search ranked by BM25.
- 2026-11-09, stage 3: `Archive` saves each writeup's page text locally
  (public hosts only, redirects checked, no code blocks), cut into FTS5
  passages; search covers it. 4 of 5 real pages saved on the first try; the
  fifth is a slow site, retried later.
- 2026-11-10, stage 4: `LlmClient` (the Ollama client, shared by `Tutor` and
  `Extractor`); `Extractor` asks the local model for concepts with a JSON
  schema in `format` (limits in the schema, or qwen3:8b loops), checks the
  answer again, and saves it in database version 3 with provenance (model,
  page or summary). Relations are concepts named in the same passage, found by
  the app, with that passage: qwen3:8b's own typed relations were noise (all
  from one concept, passages made up), so typed edges wait for a better model.
  Names and aliases merge concepts across writeups. It runs only while the
  model is ready and steps aside while the reader asks the tutor.

- 2026-11-10, stage 5: the Concepts view (G, `GraphView.qml`): a force
  layout of the library's concepts (laid out in C++ by `Backend::layout`,
  drawn with a Canvas and items, no new dependency), concept search, an
  inspector with the writeups naming the concept in date order (the
  timeline), its neighbours by shared passages with each passage, and a jump
  to the writeup. Graph queries live in `Database` (`graph`, `conceptInfo`)
  rather than a separate `Knowledge` class: they are plain SQL over the
  version-3 tables. Every view of it says the concepts are the model's; with
  no model, it says what's missing instead of an empty map.

- 2026-11-10, change of plan (the user): the local model is too slow on this
  machine for a tutor, so stage 6 (tutor modes) is dropped and stages 7–8 are
  replaced. Stages 4–5 stay; extraction is spread over two hours a batch and
  stops off AC power. Done the same day, without the model and without more
  Claude usage:
  - **Review:** spaced repetition (Leitner, 1/3/7/16/35 days) over the
    self-check questions, from the `events` history; review days count for
    the streak.
  - **Open references:** MITRE ATT&CK, CWE, CAPEC, OWASP Top 10 2025 built
    into the app (`bin/references`): "Terms in this writeup" as a glossary,
    and a concept map from them that works on every install.
  - **Levels:** the reader's level first in each day and in review; the
    prompt asks for a Junior and a Senior piece a day, security first.
  - **Labs:** hand-picked legal labs per direction and level.
  - **Notes:** per writeup, into an Obsidian/Markdown folder (never
    overwriting the reader's edits) or Notion.
  - **Help and Get started.**
  - **Site:** levels, terms, labs, review in localStorage, an Atom feed.

## 7–9. Roadmap

| # | Stage | Size | Needs |
|---|---|---|---|
| 0 | Bugs: StartLimit in `[Unit]`; category taken from the day, not `progress.json`; canonical URL comparison | S, half a day | — |
| 1 | Record self-check results (knew / didn't know) in focus mode — first real learning data, no new storage beyond `progress.json` | S | — |
| 2 | Data foundation: `Library`, SQLite, migrations, `progress.json` import, FTS5 search replacing substring search | M, 2–3 days | 0 |
| 3 | Article text: download on save/open, HTML → text, passages, cache | M, 2 days | 2 |
| 4 | Knowledge extraction with Ollama: `LlmClient`, `Extractor` queue, schema validation, provenance | M–L, 3 days | 2, 3 |
| 5 | Knowledge graph view: search, node inspector, neighbours, writeups per concept, timeline | L, 3–4 days | 4 |
| 6 | Learning engine: tutor modes, teach-back and knowledge checks with structured grading, history in the database | L, 3–4 days | 2 (better with 4) |
| 7 | Adaptive feed: state per concept, weak areas, ranking with an explanation | M, 2 days | 4, 6 |
| 8 | Notes and RAG: notes linked to concepts, retrieval over passages with citations and "not enough in your library" | M, 2–3 days | 3 (better with 4) |
| 9 | Polish: dashboard, empty/error states, screenshots, README, CLAUDE.md | M, 2 days | all |

Each stage ends with `bin/test`, a check that old days and `progress.json`
still load, and a CLAUDE.md update.

## 10. Files

- Change: `src/store.{h,cpp}`, `src/backend.{h,cpp}`, `src/progress.{h,cpp}`,
  `src/tutor.{h,cpp}`, `src/main.cpp`, `src/Main.qml`, `src/FocusView.qml`,
  `src/QuestionCard.qml`, `src/i18n/{ru,uk}.json`, `writeuppulse.pro`,
  `tests/writeuppulse_tests.cpp`, `pkgbuild/writeuppulse.service`,
  `CLAUDE.md`, `README.md`. `src/fetcher.cpp` stays as it is (budget).
- New: `src/library.{h,cpp}`, `src/llmclient.{h,cpp}`,
  `src/extractor.{h,cpp}`, `src/knowledge.{h,cpp}`, `src/learner.{h,cpp}`;
  QML `Dashboard.qml`, `LibraryView.qml`, `GraphView.qml`,
  `LearningView.qml`.

## 11. The database in a free cloud (done 2026-11-10)

Built as below with Cloudflare Workers + D1 and a sync key; see CLAUDE.md, Cloud sync. Goal it would serve: the same progress, answers,
review and notes on several machines (and maybe on the site).

- **Don't move the SQLite file.** `library.db` holds an index anyone can rebuild
  (days, FTS, passages, concepts) next to the user's own data. Only the user's part
  needs to travel: `progress`, `events`, `notes` (a few KB per user). Search and the
  map stay local.
- **Sync, not a remote database.** The window keeps working offline on its local
  SQLite and exchanges changes: `events` are append-only (push new rows, pull the
  others', dedupe by a client-generated id); `progress` and `notes` are last write
  wins by a timestamp per row (needs `updated_at` columns: a new migration).
- **An API between the app and the database.** The app is open source: a database
  token in it would be everyone's. A tiny HTTP service checks who is asking and
  touches only that user's rows.
- **Who the user is.** GitHub sign-in (OAuth device flow fits a desktop app: no
  redirect URL), or, simpler, a random sync key shown once and typed on the second
  machine. Notes are personal: encrypt them on the machine with a key derived from
  the sync key, so the server stores ciphertext.
- **Free hosting that fits** (limits change; check before choosing):
  Cloudflare Workers + D1 (SQLite dialect, same SQL as here, no sleeping);
  Turso (libSQL over HTTP, generous free tier); Supabase or Neon (Postgres; Supabase
  pauses an idle free project after a week). Render's free web services sleep and
  take ~1 min to wake: poor for a sync on open.
- **Work:** a `sync_id` + `updated_at` migration, a `CloudSync` class beside `Syncer`
  (QNetworkAccessManager, the same size caps and timeouts), the Worker (~200 lines),
  a Settings section, tests against a local stand-in server. Claude is not involved,
  so the budget rule holds.
