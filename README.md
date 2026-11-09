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

## Learning

Each writeup comes with its direction (Web, Pwn, Reverse, Crypto, Cloud Security,
Red Team, Blue Team, Malware, OSINT, LLM & Agents, ML Engineering, Systems &
Performance, Backend & Databases, DevOps & Infra, Postmortems, Other) and a level
(Junior, Middle, Senior). The deep view adds 5–8 takeaways, self-check questions
with hidden answers, and a "Try it yourself" checklist of practice ideas for a lab
you control. WriteupPulse is for understanding, not attacking: the agent is told
never to write code, commands, payloads, or exploits, and the app cuts out any
code block that slips through.

Mark writeups as read (M), done in a lab (B), or favorite (F), and tag favorites.
Filters narrow a day by direction, level, and read state; search covers every
saved day. Your progress lives in `~/.local/share/writeuppulse/progress.json`,
and Statistics shows the reading streak, counts, and directions you read most.

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
| Space / Enter | Expand or collapse the writeup (quick view) |
| D | Switch between quick and deep view |
| ↑ ↓ (K J) | Previous or next writeup |
| ← → | Older or newer day |
| O | Open the article in the browser |
| M | Mark as read |
| B | Mark as done in a lab |
| F | Add to or remove from favorites |
| H | Hide or show read writeups |
| Ctrl+F or / | Search all saved writeups |
| S | Statistics |
| R | Fetch today's writeups now |
| ? | Show or hide shortcuts |
| Q | Quit |

## Develop

```sh
bin/build    # build/writeuppulse
bin/test     # Qt Test, offscreen
```
