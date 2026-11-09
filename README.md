# WriteupPulse

![WriteupPulse showing today's writeups](docs/screenshot.png)

Every morning WriteupPulse has Claude Code look through Hacker News, r/netsec,
r/programming, lobste.rs, GitHub Trending, and engineering blogs. It picks the
five most interesting fresh technical writeups (security research, CTF, AI,
engineering deep dives, postmortems), reads them, and summarizes each one. The
window shows today's picks and every earlier day.

The agent gets only web search and web fetch. It has no shell and no file
access, and it answers with JSON that WriteupPulse checks before saving.

## Install

Needs [Claude Code](https://claude.com/claude-code), signed in (`claude`).

```sh
bin/install
```

This builds the package, installs it with `makepkg -fsi`, and turns on the
daily timer. WriteupPulse then shows up in the app launcher (`Super + Space`).

## How it runs

- `writeuppulse.timer` starts `writeuppulse.service` every day at 08:00. If the
  machine was off then, it runs on the next boot. Turn it off and on with the
  switch in the window, or with `systemctl --user disable --now writeuppulse.timer`.
- **Refresh now** starts the same service, so a fetch keeps going after the
  window closes. A fetch takes a few minutes. Logs: `journalctl --user -u writeuppulse`.
- Digests are saved as `~/.local/share/writeuppulse/<date>.json` and `<date>.md`.

The language picker at the top switches the window between English, Russian,
and Ukrainian, and the agent writes new summaries in the same language; days
already saved stay in the language they were written in. Until a language is
picked, WriteupPulse follows Omarchy's when it is one of the three.

Other settings come from the environment: `WRITEUPPULSE_MODEL` (default `sonnet`),
`WRITEUPPULSE_CLAUDE` (path to `claude` if it isn't found), and
`WRITEUPPULSE_DATA` (the data directory).

## Commands

| Command | What it does |
| --- | --- |
| `writeuppulse` / `writeuppulse open` | Open the window |
| `writeuppulse fetch [--json]` | Run the agent now and save today's digest |
| `writeuppulse list [--json]` | List the saved days |
| `writeuppulse show [DATE] [--json]` | Print a day's digest |
| `writeuppulse skill install` | Install a Claude Code skill for reading digests |

## Shortcuts

| Key | Action |
| --- | --- |
| Space / Enter | Expand or collapse the writeup |
| ↑ ↓ (K J) | Previous or next writeup |
| ← → (H L) | Older or newer day |
| O | Open the article in the browser |
| R | Fetch today's writeups now |
| ? | Show or hide shortcuts |
| Q | Quit |

## Develop

```sh
bin/build    # build/writeuppulse
bin/test     # Qt Test, offscreen
```
