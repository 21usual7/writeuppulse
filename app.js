// The public WriteupPulse site: reads data/index.json and data/<date>.json,
// written by `writeuppulse publish`. Everything in them comes from the agent,
// so it only ever goes into the page as text, and only web links are links.
'use strict';

const TOPICS = { 'Security': '#e57b8c', 'AI / ML': '#9d8cf2', 'Engineering': '#e2a46b', 'Other': '#8a94a3' };
const DATE = /^\d{4}-\d{2}-\d{2}$/;
const days = new Map();
let index = [];
let topic = '';

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
  return el('li', {}, el('a', { class: 'card', href: '#/' + date + '/' + number, topic: topicOf(item) },
    meta(item),
    el('h3', {}, text(item.title)),
    summary && el('p', {}, summary)));
}

async function renderDay(date) {
  markDay(date);
  const digest = await day(date);
  const items = list(digest.items);
  const present = Object.keys(TOPICS).filter(name => items.some(item => topicOf(item) === name));
  const chip = (label, value) => el('button', {
    class: 'chip', type: 'button', 'aria-pressed': String(topic === value),
    onclick: () => { topic = value; renderDay(date); },
  }, label);
  const shown = items.map((item, i) => [item, i + 1]).filter(([item]) => !topic || topicOf(item) === topic);
  show(
    el('h1', {}, longDate(date)),
    el('p', { class: 'sub' }, items.length + (items.length === 1 ? ' writeup' : ' writeups')),
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
  show(
    el('a', { class: 'back', href: '#/' + date }, '← ' + longDate(date)),
    el('article', {},
      meta(item),
      el('h1', {}, text(item.title)),
      text(item.why) && el('p', { class: 'why' }, text(item.why)),
      el('div', { class: 'prose' }, text(item.summary).split(/\n\s*\n/).map(paragraph => el('p', {}, paragraph.trim()))),
      url && el('a', { class: 'open', href: url, rel: 'noopener noreferrer nofollow', target: '_blank' }, 'Read the original'),
      section('Takeaways', points('ul', list(item.takeaways))),
      section('Check yourself', questions.length ? questions.map(entry =>
        el('details', {}, el('summary', {}, text(entry.question)), el('p', {}, text(entry.answer)))) : null),
      section('Try it yourself', points('ol', list(item.practice))),
      list(item.practice).length > 0 && el('p', { class: 'note' }, 'Only in a lab you own or are allowed to test.'),
      list(item.tags).length > 0 && el('div', { class: 'tags' }, list(item.tags).map(tag => el('span', { class: 'tag' }, text(tag)))),
      el('nav', { class: 'pager' }, neighbour(number - 1, '← '), neighbour(number + 1, '→ '))));
  window.scrollTo(0, 0);
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
  route();
}

start();
