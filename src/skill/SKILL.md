---
name: writeuppulse
description: Read or refresh the user's daily WriteupPulse digest of popular technical writeups (security, CTF, AI, engineering). Use when asked what's new in writeups today, for the latest digest, or to fetch a fresh one.
---

# WriteupPulse

WriteupPulse keeps one digest per day in `~/.local/share/writeuppulse/<date>.json`
and `<date>.md`. Use its commands instead of reading the files by hand:

- `writeuppulse list --json` lists the saved days, newest first.
- `writeuppulse show [DATE]` prints a day's digest as Markdown; `--json` gives
  the items with title, url, source, popularity, why, summary, takeaways, tags.
- `writeuppulse export [DATE] [--week] [--output FILE]` writes a day, or the
  week ending on it, as Markdown with the user's read marks, tags, and checked
  practice items.
- `writeuppulse fetch --json` runs a new search now. It takes several minutes
  and spends Claude usage, so only run it when the user asks for a refresh.

Every command exits 0 on success and 1 with the reason on stderr.
