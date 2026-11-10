# WriteupPulse

![WriteupPulse: the day's writeups on the left, the open one on the right](docs/screenshot.png)

Every morning WriteupPulse has Claude Code look through Hacker News, r/netsec,
r/programming, lobste.rs, GitHub Trending, and engineering blogs. It picks the
five most interesting fresh technical writeups (security research, CTF, AI,
engineering deep dives, postmortems), reads them, and summarizes each one. The
window shows today's picks and every earlier day.

The agent gets only web search and web fetch. It has no shell and no file
access, and it answers with JSON that WriteupPulse checks before saving.

You don't need an agent of your own: the daily picks are published at
[21usual7.github.io/writeuppulse](https://21usual7.github.io/writeuppulse/), and
the app downloads them from there by default.

## Install

Out of the box, WriteupPulse downloads the writeups published on
[the WriteupPulse site](https://21usual7.github.io/writeuppulse/): nothing else
is needed. To run your own agent instead, install
[Claude Code](https://claude.com/claude-code), sign in (`claude`), and pick
**My Claude Code** under **Writeups from** in Settings (P).

On Arch or Omarchy, download the package from
[Releases](https://github.com/21usual7/writeuppulse/releases/latest), then:

```sh
sudo pacman -U writeuppulse-*.pkg.tar.zst
systemctl --user enable --now writeuppulse.timer
```

Or build it from source:

```sh
git clone https://github.com/21usual7/writeuppulse.git
cd writeuppulse
bin/install
```

This builds the package, installs it with `makepkg -fsi`, and turns on the
daily timer. Either way, WriteupPulse then shows up in the app launcher
(`Super + Space`).

## How it runs

- `writeuppulse.timer` starts `writeuppulse.service` (`writeuppulse update`:
  download from the site, or run your agent) every day at 08:00. If the machine
  was off then, it runs on the next boot. Turn it off and on with the switch in
  the window, or with `systemctl --user disable --now writeuppulse.timer`.
- While the window is open and it reads the site, it also checks for new days
  every hour.
- **Refresh now** starts the same service, so a fetch keeps going after the
  window closes. A fetch takes a few minutes. Logs: `journalctl --user -u writeuppulse`.
- Digests are saved as `~/.local/share/writeuppulse/<date>.json` and `<date>.md`.

## Reading and learning

The day's writeups sit in a list on the left, each with its direction (Web, Pwn,
Reverse, Crypto, Cloud Security, Red Team, Blue Team, Malware, OSINT, LLM & Agents,
ML Engineering, Systems & Performance, Backend & Databases, DevOps & Infra,
Postmortems, Other) and level (Junior, Middle, Senior). The open one is on the
right, in four tabs: the summary, 5–8 takeaways, self-check questions with hidden
answers, and a "Try it yourself" checklist of practice ideas for a lab you control.

**Ask** (5) is a chat with a local tutor about the open writeup, and **Discuss**
next to a practice idea asks it to review what you did in your lab and suggest the
next step. The tutor runs on your machine with [Ollama](https://ollama.com)
(`sudo pacman -S ollama`, `systemctl enable --now ollama`, `ollama pull qwen3:8b`;
pick another model in Settings), knows the writeup's summary, takeaways, questions,
and practice ideas, and follows the same rule: explanations in words, no code or
commands. Conversations last until the window closes.

**Study step by step** (Enter) opens focus mode: one writeup at a time, through
read → remember → check yourself → practice, with the day's progress on top. After
revealing an answer, say whether you knew it; Statistics counts the answers you knew.

![Focus mode, checking yourself](docs/focus.png)

**Review** (V) brings back the self-check questions of earlier writeups, a few a
day, spaced out like flash cards: one you knew returns after 1, 3, 7, 16, then 35
days, one you didn't returns tomorrow, and up to five new ones join daily, from
writeups you marked read first. Reveal with Space, answer with Y or N. A day of
review keeps the streak going.

Under each summary, **Terms in this writeup** lists the MITRE ATT&CK techniques, CWE
weaknesses, CAPEC attack patterns, and OWASP Top 10 2025 categories the writeup (and
its saved page) names, each with its official definition and a link to read more:
a glossary for beginners, and the standard vocabulary for everyone.

**Concepts** (G) is a map of what your library teaches. By default it shows those
references: every entry your writeups name, related when one passage names two of
them or when the catalogs relate them (a weakness to its attack patterns, a pattern
to ATT&CK techniques, an OWASP category to its weaknesses). It is ready at once on
every install. Switch it to the local model to see the model's concepts instead. While Ollama is running,
the local model reads each saved writeup in the background (a few minutes each on
a CPU) and lists the concepts, techniques, tools, vulnerabilities, and technologies
in it; two concepts are related when one passage of the article names both. Search
the map, pick a concept to see the writeups that mention it in date order and its
related concepts with the passages that relate them, and open a writeup from there.
It is the model's reading, not a set of facts, and the window says so. Without
Ollama that map stays empty and says what it needs; everything else works. The
model reads one writeup at a time, spread over two hours, and only on AC power.

WriteupPulse is for understanding, not attacking: the agent is told never to write
code, commands, payloads, or exploits, and the app cuts out any code block that
slips through.

Mark writeups as read (M), done in a lab (B), or favorite (F), and tag them.
Filter the list by topic, level, or unread; search covers every saved day, matches
the beginnings of words, and puts title matches first. With "Save article texts for
search" on (Settings, on by default), the window downloads each writeup's page once
from the site that published it and keeps its text, without code blocks, on your
machine, so search also covers whole articles. Only public web addresses are
fetched, and nothing is sent to Claude.
The Practice tab also suggests where to try it legally: two or three labs for the
writeup's direction and level, open-source ones you run yourself first (OWASP Juice
Shop, WebGoat, DVWA, crAPI, pwn.college, how2heap, CloudGoat, Kubernetes Goat, GOAD,
Atomic Red Team, Security Datasets, Security Onion, FLARE-VM, REMnux, garak, PyRIT),
then free online ones (OverTheWire, picoCTF, Cryptopals, flAWS).

**Notes** (6) is a page of your own on each writeup. Notes stay in the app, and,
if you pick a notes folder in Settings (an Obsidian vault, or any folder a Markdown
app reads), each one is also saved there as `WriteupPulse/<date> <title>.md` with the
summary, takeaways, tags, and the writeup's ATT&CK/CWE terms as `[[links]]`, so they
join your own knowledge graph. A file you edit in Obsidian is yours: the app never
overwrites it. To keep notes in Notion, create an integration at
notion.so/my-integrations, share a page with it, and paste its token and the page's
link into Settings; "Send to Notion" then adds the writeup and your note as a page.

The first start walks through **Get started**: what the app is, your level and
language, how a day of learning goes, and what stays on your computer. **Help** (?)
has the same and more, every shortcut, and a way back to Get started.

Pick your level in Settings (Junior, Middle, or Senior): each day lists the
writeups at your level first, and review starts new questions from them. Every day
has at least one piece a beginner can follow and one deep dive, security first.
Settings (P) holds the language, the daily timer, and which directions the agent
searches, with stars for the ones it should pick more often. Export (E) saves the
day, or the week ending on it, as Markdown through the desktop's save dialog, with
your read marks, tags, checked practice items, and self-check answers. Progress
lives in a local SQLite file, `~/.local/share/writeuppulse/library/library.db`
(a `progress.json` from earlier versions is imported once and left as a backup);
Statistics shows the streak and what you read most.

Other settings come from the environment: `WRITEUPPULSE_MODEL` (default `sonnet`),
`WRITEUPPULSE_CLAUDE` (path to `claude` if it isn't found),
`WRITEUPPULSE_FEED` (the site's data URL, for a fork), and
`WRITEUPPULSE_DATA` (the data directory).

## Commands

| Command | What it does |
| --- | --- |
| `writeuppulse` / `writeuppulse open` | Open the window |
| `writeuppulse update [--json]` | Download from the site or run the agent, as set in Settings |
| `writeuppulse sync [--json]` | Download new days from the site |
| `writeuppulse fetch [--json]` | Run the agent now and save today's digest |
| `writeuppulse list [--json]` | List the saved days |
| `writeuppulse show [DATE] [--json]` | Print a day's digest |
| `writeuppulse export [DATE] [--week] [--output FILE]` | Export a day or week as Markdown with your progress |
| `writeuppulse publish DIR` | Write every day, without private fields, and an index to DIR for the public site |
| `writeuppulse skill install` | Install a Claude Code skill for reading digests |

## Shortcuts

| Key | Action |
| --- | --- |
| ↑ ↓ (K J) | Previous or next writeup |
| ← → | Older or newer day; in focus mode, previous or next step |
| 1 – 6 | Summary, takeaways, questions, practice, ask the tutor, notes |
| Enter | Study the writeup step by step |
| Space | In focus mode, reveal the next answer or go on |
| O | Open the article in the browser |
| M | Mark as read |
| B | Mark as done in a lab |
| F | Add to or remove from favorites |
| U | Show only unread |
| Ctrl+F or / | Search all saved writeups |
| V | Review today's questions (Space, Y, N) |
| S | Statistics |
| G | Map of concepts |
| P | Settings and directions |
| E | Export the day or week as Markdown |
| R | Fetch today's writeups now |
| Esc | Back to the list |
| ? | Show or hide help |
| Q | Quit |

## Public site

The writeups are also published at
[21usual7.github.io/writeuppulse](https://21usual7.github.io/writeuppulse/).
`bin/publish` copies the site from `site/` and the saved days (without the run's
cost and without your progress) to the `pages` branch and pushes it.

## Develop

```sh
bin/build    # build/writeuppulse
bin/test     # Qt Test, offscreen
```

## Reference data

`src/references/references.json` is built by `bin/references` from MITRE ATT&CK
(Enterprise), CWE, and CAPEC, © The MITRE Corporation, reproduced and distributed with
its permission under their terms of use
([ATT&CK](https://attack.mitre.org/resources/legal-and-branding/terms-of-use/),
[CWE](https://cwe.mitre.org/about/termsofuse.html),
[CAPEC](https://capec.mitre.org/about/termsofuse.html)), and the OWASP Top 10 2025
categories as CWE maps them (OWASP Top 10 © the OWASP Foundation, CC BY-SA 4.0).
Run `bin/references` to refresh them to the latest releases.
