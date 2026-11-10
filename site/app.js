// The public WriteupPulse site: reads data/index.json and data/<date>.json,
// written by `writeuppulse publish`. Everything in them comes from the agent,
// so it only ever goes into the page as text, and only web links are links.
'use strict';

const TOPICS = { 'Security': '#e57b8c', 'AI / ML': '#9d8cf2', 'Engineering': '#e2a46b', 'Other': '#8a94a3' };
const DATE = /^\d{4}-\d{2}-\d{2}$/;
const days = new Map();
const LEVELS = ['Junior', 'Middle', 'Senior'];
const KINDS = { technique: 'ATT&CK technique', weakness: 'CWE weakness', pattern: 'CAPEC attack pattern', owasp: 'OWASP Top 10 2025' };
let index = [];
let topic = '';
let labs = null;

// What the reader keeps in this browser only: their level, the writeups they
// opened, and their review answers. Nothing is sent anywhere.
function stored(key, fallback) {
  try {
    const value = JSON.parse(localStorage.getItem('wp.' + key));
    return value == null ? fallback : value;
  } catch {
    return fallback;
  }
}

function store(key, value) {
  try {
    localStorage.setItem('wp.' + key, JSON.stringify(value));
  } catch {
    // Private windows may refuse; the page still works without it.
  }
}

function today() {
  const now = new Date();
  return new Date(now.getTime() - now.getTimezoneOffset() * 60000).toISOString().slice(0, 10);
}

function addDays(date, n) {
  const d = new Date(date + 'T12:00:00Z');
  d.setUTCDate(d.getUTCDate() + n);
  return d.toISOString().slice(0, 10);
}

function el(tag, props, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props || {})) {
    if (value == null || value === false) continue;
    if (key === 'class') node.className = value;
    else if (key === 'topic') node.style.setProperty('--topic', TOPICS[value] || TOPICS.Other);
    else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value);
  }
  for (const child of children.flat())
    if (child != null && child !== false && child !== '') node.append(child);
  return node;
}

function webUrl(text) {
  try {
    const url = new URL(text);
    return url.protocol === 'https:' || url.protocol === 'http:' ? url.href : null;
  } catch {
    return null;
  }
}

function text(value) {
  return typeof value === 'string' ? value : '';
}

function list(value) {
  return Array.isArray(value) ? value : [];
}

function longDate(date) {
  return new Date(date + 'T12:00:00').toLocaleDateString('en-GB', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' });
}

function shortDate(date) {
  return new Date(date + 'T12:00:00').toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' });
}

function topicOf(item) {
  return TOPICS[item.topic] ? item.topic : 'Other';
}

function meta(item) {
  return el('div', { class: 'meta', topic: topicOf(item) },
    el('span', { class: 'kind' }, text(item.category) || topicOf(item)),
    text(item.level) && el('span', {}, text(item.level)),
    text(item.source) && el('span', {}, text(item.source)),
    text(item.popularity) && el('span', {}, text(item.popularity)));
}

async function loadJson(path) {
  const response = await fetch(path, { cache: 'no-cache' });
  if (!response.ok) throw new Error(path + ': ' + response.status);
  return response.json();
}

async function day(date) {
  if (!days.has(date)) days.set(date, loadJson('data/' + date + '.json'));
  return days.get(date);
}

function show(...children) {
  const main = document.getElementById('main');
  main.replaceChildren(...children.flat().filter(Boolean));
  return main;
}

function markDay(date) {
  for (const link of document.querySelectorAll('#days a'))
    link.toggleAttribute('aria-current', link.dataset.date === date);
}

function renderDays() {
  document.getElementById('days').replaceChildren(...index.map(entry =>
    el('li', {}, el('a', { href: '#/' + entry.date, 'data-date': entry.date },
      el('span', { class: 'date' }, shortDate(entry.date)),
      el('span', { class: 'dots', 'aria-hidden': 'true' },
        list(entry.items).map(item => el('span', { class: 'dot', topic: topicOf(item) })))))));
}

function card(date, item, number, summary) {
  const mine = stored('level', '');
  return el('li', {}, el('a', { class: 'card', href: '#/' + date + '/' + number, topic: topicOf(item) },
    meta(item),
    mine && text(item.level) === mine && el('span', { class: 'yours' }, 'your level'),
    el('h3', {}, text(item.title)),
    summary && el('p', {}, summary)));
}

function levelChips(refresh) {
  const mine = stored('level', '');
  const chip = (label, value) => el('button', {
    class: 'chip', type: 'button', 'aria-pressed': String(mine === value),
    onclick: () => { store('level', value); refresh(); },
  }, label);
  return el('div', { class: 'chips' }, el('span', { class: 'label' }, 'My level'), chip('Any', ''), LEVELS.map(level => chip(level, level)));
}

async function renderDay(date) {
  markDay(date);
  const digest = await day(date);
  const mine = stored('level', '');
  // The reader's level first, like in the app.
  const items = list(digest.items);
  const present = Object.keys(TOPICS).filter(name => items.some(item => topicOf(item) === name));
  const chip = (label, value) => el('button', {
    class: 'chip', type: 'button', 'aria-pressed': String(topic === value),
    onclick: () => { topic = value; renderDay(date); },
  }, label);
  const shown = items.map((item, i) => [item, i + 1]).filter(([item]) => !topic || topicOf(item) === topic)
    .sort(([a], [b]) => (text(b.level) === mine && !!mine) - (text(a.level) === mine && !!mine));
  show(
    el('h1', {}, longDate(date)),
    el('p', { class: 'sub' }, items.length + (items.length === 1 ? ' writeup' : ' writeups')),
    levelChips(() => renderDay(date)),
    present.length > 1 && el('div', { class: 'chips' }, chip('All', ''), present.map(name => chip(name, name))),
    shown.length ? el('ol', { class: 'cards' }, shown.map(([item, number]) => card(date, item, number, text(item.why))))
                 : el('p', { class: 'muted' }, 'Nothing in this topic today.'));
}

async function renderItem(date, number) {
  markDay(date);
  const items = list((await day(date)).items);
  const item = items[number - 1];
  if (!item) return renderDay(date);
  const url = webUrl(text(item.url));
  const section = (title, body) => body && el('section', {}, el('h2', {}, title), body);
  const points = (tag, values) => values.length ? el(tag, {}, values.map(value => el('li', {}, text(value)))) : null;
  const questions = list(item.questions).filter(entry => entry && text(entry.question));
  const neighbour = (n, label) => items[n - 1] ? el('a', { href: '#/' + date + '/' + n }, label + text(items[n - 1].title)) : el('span');
  const seen = stored('seen', {});
  if (url && !seen[url]) {
    seen[url] = today();
    store('seen', seen);
  }
  const terms = list(item.terms).filter(term => term && text(term.id) && text(term.name));
  const places = (await labsFor(text(item.category), text(item.level)));
  show(
    el('a', { class: 'back', href: '#/' + date }, '← ' + longDate(date)),
    el('article', {},
      meta(item),
      el('h1', {}, text(item.title)),
      text(item.why) && el('p', { class: 'why' }, text(item.why)),
      el('div', { class: 'prose' }, text(item.summary).split(/\n\s*\n/).map(paragraph => el('p', {}, paragraph.trim()))),
      url && el('a', { class: 'open', href: url, rel: 'noopener noreferrer nofollow', target: '_blank' }, 'Read the original'),
      section('Takeaways', points('ul', list(item.takeaways))),
      section('Terms in this writeup', terms.length ? terms.map(term => {
        const link = webUrl(text(term.url));
        return el('details', { class: 'term' }, el('summary', {}, text(term.name), el('span', { class: 'id' }, text(term.id) + ' · ' + (KINDS[term.kind] || ''))),
          el('p', {}, text(term.text)),
          link && el('p', {}, el('a', { href: link, rel: 'noopener noreferrer nofollow', target: '_blank' }, (term.kind === 'owasp' ? 'Read at OWASP' : 'Read at MITRE') + ' ↗')));
      }) : null),
      section('Check yourself', questions.length ? questions.map(entry =>
        el('details', {}, el('summary', {}, text(entry.question)), el('p', {}, text(entry.answer)),
          url && judge(url, text(entry.question)))) : null),
      section('Try it yourself', points('ol', list(item.practice))),
      list(item.practice).length > 0 && el('p', { class: 'note' }, 'Only in a lab you own or are allowed to test.'),
      section('Where to practice legally', places.length ? el('ul', { class: 'labs' }, places.map(lab => el('li', {},
        el('a', { href: webUrl(text(lab.url)), rel: 'noopener noreferrer nofollow', target: '_blank' }, text(lab.name) + ' ↗'),
        el('span', { class: 'id' }, (lab.open ? 'open source, on your machine' : 'free online') + ' · ' + list(lab.levels).map(text).join(', ')),
        el('p', {}, text((lab.about || {}).en))))) : null),
      list(item.tags).length > 0 && el('div', { class: 'tags' }, list(item.tags).map(tag => el('span', { class: 'tag' }, text(tag)))),
      el('nav', { class: 'pager' }, neighbour(number - 1, '← '), neighbour(number + 1, '→ '))));
  window.scrollTo(0, 0);
}

async function labsFor(category, level) {
  if (labs === null) {
    try {
      labs = list((await loadJson('data/labs.json')).labs).filter(lab => lab && webUrl(text(lab.url)));
    } catch {
      labs = [];
    }
  }
  const rank = lab => 2 * list(lab.levels).includes(level) + (lab.open === true);
  return labs.filter(lab => list(lab.directions).includes(category)).sort((a, b) => rank(b) - rank(a)).slice(0, 3);
}

// Review, Leitner style, as in the app: a question you knew comes back after
// 1, 3, 7, 16, then 35 days; one you didn't, tomorrow. Up to five new ones a
// day, from writeups you opened first.
const INTERVALS = [0, 1, 3, 7, 16, 35];

function answer(key, result) {
  const cards = stored('cards', {});
  const card = cards[key] || { box: 0 };
  cards[key] = { box: result === 'knew' ? Math.min(5, card.box + 1) : 1, last: today(), first: card.first || today() };
  store('cards', cards);
}

function judge(url, question, after) {
  const key = url + '\n' + question;
  const result = el('span', { class: 'id' });
  const button = (label, value) => el('button', { class: 'chip', type: 'button', onclick: () => {
    answer(key, value);
    result.textContent = value === 'knew' ? 'Back in a few days.' : 'Back tomorrow.';
    if (after) after();
  } }, label);
  return el('div', { class: 'judge' }, button('I knew it', 'knew'), button("I didn't", 'missed'), result);
}

async function reviewCards() {
  const cards = stored('cards', {});
  const seen = stored('seen', {});
  const mine = stored('level', '');
  const due = [], fresh = [];
  let introduced = Object.values(cards).filter(card => card.first === today()).length;
  const recent = index.slice(0, 30);
  for (const entry of recent) {
    let digest;
    try {
      digest = await day(entry.date);
    } catch {
      continue;
    }
    list(digest.items).forEach((item, i) => {
      const url = webUrl(text(item.url));
      if (!url) return;
      for (const q of list(item.questions)) {
        if (!q || !text(q.question) || !text(q.answer)) continue;
        const key = url + '\n' + text(q.question);
        const card = { key, url, date: entry.date, number: i + 1, item, question: text(q.question), answer: text(q.answer) };
        const state = cards[key];
        if (state) {
          if (addDays(state.last, INTERVALS[state.box]) <= today()) due.push(card);
        } else if (seen[url] || entry.date < today()) {
          card.rank = (seen[url] ? 2 : 0) + (mine && text(item.level) === mine ? 1 : 0);
          fresh.push(card);
        }
      }
    });
  }
  fresh.sort((a, b) => b.rank - a.rank);
  return due.concat(fresh.slice(0, Math.max(0, 5 - introduced)));
}

async function renderReview() {
  markDay('');
  const cards = await reviewCards();
  updateReviewCount(cards.length);
  const card = cards[0];
  const answerBlock = el('div', { class: 'answer', hidden: '' }, el('p', {}, card ? card.answer : ''),
    card && judge(card.url, card.question, () => renderReview()));
  show(
    el('h1', {}, 'Review'),
    el('p', { class: 'sub' }, cards.length + (cards.length === 1 ? ' question' : ' questions') + ' left for today. Your answers stay in this browser.'),
    card ? el('div', { class: 'review' },
      el('a', { class: 'from', href: '#/' + card.date + '/' + card.number }, text(card.item.title)),
      el('h2', { class: 'question' }, card.question),
      el('button', { class: 'open', type: 'button', onclick: event => { answerBlock.hidden = false; event.target.remove(); } }, 'Reveal the answer'),
      answerBlock)
      : el('p', { class: 'muted' }, 'Nothing to review right now. Open a writeup and come back tomorrow: its questions return here, spaced out, until you know them.'),
    el('p', { class: 'note' }, 'A question you knew comes back after 1, 3, 7, 16, then 35 days; one you didn\'t, tomorrow. Up to five new questions join a day, from writeups you opened first.'));
}

function updateReviewCount(count) {
  const badge = document.getElementById('review-count');
  if (badge) badge.textContent = count ? String(count) : '';
}

function renderSearch(query) {
  markDay('');
  const words = query.toLowerCase().split(/\s+/).filter(Boolean);
  const results = [];
  for (const entry of index)
    list(entry.items).forEach((item, i) => {
      const haystack = [item.title, item.source, item.category, item.level, ...list(item.tags)].map(text).join(' ').toLowerCase();
      if (words.every(word => haystack.includes(word))) results.push(card(entry.date, item, i + 1, shortDate(entry.date)));
    });
  show(
    el('h1', {}, 'Search'),
    el('p', { class: 'sub' }, results.length + (results.length === 1 ? ' match' : ' matches') + ' for “' + query + '”'),
    results.length > 0 && el('ol', { class: 'cards' }, results));
}

function renderAbout() {
  markDay('');
  const p = (...parts) => el('p', {}, ...parts);
  show(el('div', { class: 'about' },
    el('h1', {}, 'How it works'),
    p('Every morning ', el('a', { href: 'https://github.com/21usual7/writeuppulse' }, 'WriteupPulse'),
      ' has Claude Code look through Hacker News, r/netsec, r/programming, lobste.rs, GitHub Trending and engineering blogs. ',
      'It picks five fresh technical writeups (security research, CTF, AI, engineering deep dives, postmortems), reads them and summarizes each one.'),
    p(el('strong', {}, 'The agent only reads. '),
      'It gets web search and web fetch and nothing else: no shell, no files. It answers with JSON that the app checks before saving, and the same check runs again before anything is published here.'),
    p(el('strong', {}, 'For learning, not attacking. '),
      'Each writeup comes with takeaways, self-check questions and practice ideas for a lab you control. The agent is told never to write code, commands, payloads or exploits, and any code block that slips through is cut out.'),
    p(el('strong', {}, 'Learn, then remember. '),
      'Pick your level and it comes first each day. Under each writeup, the MITRE ATT&CK techniques, CWE weaknesses, CAPEC patterns, and OWASP Top 10 categories it names explain themselves, and the practice ideas come with legal labs to try them in. ',
      'Answer the self-check questions and they come back in Review, spaced out, until you know them. All of that stays in your browser. Follow new days with the ', el('a', { href: 'data/feed.xml' }, 'Atom feed'), '.'),
    p(el('strong', {}, 'Summaries can be wrong. '),
      'They are written by a model. Read the original before relying on a detail, and credit its authors, not this page.'),
    p('The desktop app also keeps your own progress: what you read, did in a lab, and tagged. That stays on your machine; only the writeups are published. ',
      'To run your own, see the ', el('a', { href: 'https://github.com/21usual7/writeuppulse#install' }, 'install guide'), '.')));
}

async function route() {
  const query = document.getElementById('search').value.trim();
  const [, date, number] = location.hash.split('/');
  try {
    if (!index.length) show(el('h1', {}, 'No writeups yet'), el('p', { class: 'muted' }, 'The first day will appear after the next morning run.'));
    else if (location.hash === '#/about') renderAbout();
    else if (location.hash === '#/review') await renderReview();
    else if (query && location.hash === '#/search') renderSearch(query);
    else if (DATE.test(date || '') && index.some(entry => entry.date === date)) await (Number(number) > 0 ? renderItem(date, Number(number)) : renderDay(date));
    else await renderDay(index[0].date);
  } catch (error) {
    show(el('h1', {}, 'Could not load this day'), el('p', { class: 'muted' }, String(error.message || error)));
  }
}

async function start() {
  const search = document.getElementById('search');
  search.addEventListener('input', () => {
    if (search.value.trim()) {
      if (location.hash !== '#/search') history.replaceState(null, '', '#/search');
      renderSearch(search.value.trim());
    } else {
      route();
    }
  });
  window.addEventListener('hashchange', () => {
    if (location.hash !== '#/search') search.value = '';
    route();
  });
  try {
    index = list((await loadJson('data/index.json')).days).filter(entry => entry && DATE.test(text(entry.date)));
  } catch {
    index = [];
  }
  renderDays();
  await route();
  if (location.hash !== '#/review') reviewCards().then(cards => updateReviewCount(cards.length), () => {});
}

start();
