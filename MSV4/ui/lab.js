const $ = id => document.getElementById(id);
const state = { zero: null, token: '', sessionId: '', events: [], lastSeq: 0, connection: 'disconnected', busy: false, capturing: false, polling: false };
// Above this the electrode sees no conductive path (open air, or bone-dry media).
const RSOIL_OPEN_OHM = 1e6;

function formatOhms(v) {
  if (v >= RSOIL_OPEN_OHM) return '≥1 MΩ';
  if (v >= 1e3) return `${(v / 1e3).toFixed(v >= 1e5 ? 0 : 1)} kΩ`;
  return `${Math.round(v)} Ω`;
}

const metricIds = ['temp_c', 'die_c', 'sense_mv', 'drv_mv', 'rsoil_ohm', 'cm_pf', 'vbat_mv'];
let toastTimer;

function toast(message) {
  const box = $('toast');
  box.textContent = message;
  box.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => box.classList.remove('show'), 4200);
}

async function request(path, body) {
  const options = body === undefined ? {} : {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'X-MSV4-Token': state.token },
    body: JSON.stringify(body)
  };
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `Request failed (${response.status})`);
  return data;
}

async function act(path, body, message) {
  try {
    const result = await request(path, body);
    if (message) toast(message);
    await poll();
    return result;
  } catch (error) {
    toast(error.message);
    return null;
  }
}

function formatTime(iso) {
  const date = new Date(iso);
  return Number.isNaN(date.valueOf()) ? '' : date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function appendEvent(event) {
  state.events.push(event);
  state.lastSeq = event.seq;
  const transcript = $('transcript');
  const nearBottom = transcript.scrollTop + transcript.clientHeight >= transcript.scrollHeight - 35;
  const line = document.createElement('div');
  line.className = `event ${event.kind}`;
  const time = document.createElement('time');
  time.textContent = formatTime(event.time);
  const message = document.createElement('span');
  message.className = 'message';
  message.textContent = event.kind === 'command' ? `> ${event.text}` : event.kind === 'note' ? `NOTE  ${event.text}` : event.text;
  line.append(time, message);
  transcript.appendChild(line);
  if (nearBottom) transcript.scrollTop = transcript.scrollHeight;
  $('event-count').textContent = `${state.events.length} events`;
}

function resetSession(session) {
  state.sessionId = session.id;
  state.events = [];
  state.lastSeq = 0;
  $('transcript').replaceChildren();
  $('event-count').textContent = '0 events';
  $('session-select').value = session.id;
  for (const key of metricIds) $(`metric-${key}`).textContent = '—';
  $('last-reading').textContent = session.title;
  drawChart();
}

function renderMetrics() {
  const latest = {};
  let lastTime = '';
  for (const event of state.events) {
    if (!event.metrics) continue;
    for (const [key, value] of Object.entries(event.metrics)) {
      latest[key] = value;
      lastTime = event.time;
    }
  }
  for (const key of metricIds) {
    const value = latest[key];
    $(`metric-${key}`).textContent = value === undefined ? '—' :
      key === 'rsoil_ohm' ? (value <= 1 ? 'no fit' : value >= RSOIL_OPEN_OHM ? 'open (dry)' : formatOhms(value)) :
      (key === 'temp_c' || key === 'die_c' || key === 'sense_mv' || key === 'drv_mv' || key === 'cm_pf'
        ? Number(value).toFixed(2) : Math.round(value).toLocaleString());
  }
  if (lastTime) $('last-reading').textContent = `Last reading ${formatTime(lastTime)}`;
}

function renderStatus(data) {
  state.connection = data.connection;
  state.busy = data.busy;
  state.capturing = data.capturing;
  const pill = $('connection-pill');
  pill.className = `connection-pill ${data.connection}`;
  $('connection-text').textContent = data.connection;
  $('device-detail').textContent = data.connection === 'disconnected' ? 'No board connected' : `Board: ${data.device}`;
  $('connect-btn').disabled = data.connection !== 'disconnected';
  $('disconnect-btn').disabled = data.connection === 'disconnected';
  $('capture-btn').disabled = data.connection !== 'connected';
  $('capture-btn').textContent = data.capturing ? 'Stop capture' : 'Start capture';
  $('busy-label').textContent = data.busy ? 'Measurement in progress · stop can cancel a sweep' :
    data.capturing ? `Capturing every ${data.capture_interval}s` : 'Ready';
  for (const button of document.querySelectorAll('[data-command]')) {
    button.disabled = data.connection !== 'connected' || (data.busy && button.dataset.command !== 'stop');
  }
}

async function poll() {
  if (state.polling) return;
  state.polling = true;
  try {
    const data = await request(`/api/state?after=${state.lastSeq}`);
    if (data.session.id !== state.sessionId || data.total < state.lastSeq) {
      // Another session, or a restarted server holding fewer events than this page has seen:
      // these events were cut at the old position, so start over from the first one.
      const refetch = state.lastSeq !== 0;
      resetSession(data.session);
      if (refetch) { renderStatus(data); setTimeout(poll, 0); return; }
    }
    // Never append an event twice: a repeat lands after newer samples and the live chart jumps back.
    for (const event of data.events) if (event.seq > state.lastSeq) appendEvent(event);
    renderStatus(data);
    if (data.events.length) { renderMetrics(); drawChart(); renderLive(); }
    $('live-state').textContent = data.busy && data.connection === 'connected' ? 'Streaming or measuring · Stop ends it' : 'Not streaming';
    if (data.total > state.lastSeq) setTimeout(poll, 0);
  } catch (error) {
    $('connection-text').textContent = 'Server unavailable';
  } finally {
    state.polling = false;
  }
}

const isConnectionStart = e => e.kind === 'status' && e.text.startsWith('Connecting to');

// Each `live` stream's G and C are relative to the open reference it measured when it started, so
// samples from an earlier stream, or from before a reconnect, sit on another baseline. The live
// panel shows only the newest stream: the events after the last "live started" line or new
// connection. Returns that boundary's index (-1: the whole session).
function liveStart() {
  for (let i = state.events.length - 1; i >= 0; i--) {
    const e = state.events[i];
    if (isConnectionStart(e) || (e.kind === 'rx' && e.text.startsWith('live started'))) return i;
  }
  return -1;
}

function lastLive(start = liveStart()) {
  for (let i = state.events.length - 1; i > start; i--) {
    const m = state.events[i].metrics;
    if (m && Number.isFinite(m.live_g_us)) return { g: m.live_g_us, c: m.live_c_pf, t: new Date(state.events[i].time).valueOf() };
  }
  return null;
}

// The zero belongs to the stream it was taken in: a new stream has a new baseline.
function liveZero(start) {
  const id = start >= 0 ? state.events[start].seq : 0;
  return state.zero && state.zero.stream === id ? state.zero.g : null;
}

// Last 60 s of the live stream, right edge = newest sample, so the picture holds still once stopped.
function renderLive() {
  const start = liveStart();
  const last = lastLive(start);
  const zero = liveZero(start);
  $('live-g').textContent = last ? last.g.toFixed(1) : '—';
  $('live-c').textContent = last && Number.isFinite(last.c) ? last.c.toFixed(2) : '—';
  $('live-dg').textContent = last && zero !== null ? (last.g - zero >= 0 ? '+' : '') + (last.g - zero).toFixed(1) : '—';
  let rs = null;
  for (let i = state.events.length - 1; i >= 0 && !isConnectionStart(state.events[i]); i--) {
    const m = state.events[i].metrics;
    if (m && Number.isFinite(m.rsoil25_ohm)) { rs = m.rsoil25_ohm; break; }
  }
  $('live-rsoil').textContent = rs === null ? '—' : rs <= 1 ? 'no fit' : rs >= RSOIL_OPEN_OHM ? 'open (dry)' : formatOhms(rs);
  drawLive(last, start, zero || 0);
}

function drawLive(last, start, zero) {
  const canvas = $('live-chart');
  const box = canvas.getBoundingClientRect();
  if (!box.width || !box.height) return;
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.round(box.width * scale);
  canvas.height = Math.round(box.height * scale);
  const ctx = canvas.getContext('2d');
  ctx.scale(scale, scale);
  const WINDOW_MS = 60000;
  const points = [];
  if (last) {
    for (let i = state.events.length - 1; i > start; i--) {
      const e = state.events[i];
      if (!e.metrics || !Number.isFinite(e.metrics.live_g_us)) continue;
      const t = new Date(e.time).valueOf();
      if (last.t - t > WINDOW_MS) break;
      points.push({ x: t, y: e.metrics.live_g_us - zero });
    }
    points.reverse();
  }
  $('live-empty').style.display = points.length > 1 ? 'none' : 'grid';
  if (points.length < 2) return;
  const w = box.width, h = box.height, left = 58, right = 17, top = 16, bottom = 28;
  const values = points.map(p => p.y);
  let min = Math.min(...values, 0), max = Math.max(...values, 0);
  const pad = Math.max((max - min) * .12, 1);
  min -= pad; max += pad;
  const x1 = last.t, x0 = x1 - WINDOW_MS;
  const xp = x => left + (x - x0) / (x1 - x0) * (w - left - right);
  const yp = y => top + (max - y) / (max - min) * (h - top - bottom);
  ctx.font = '11px ui-monospace, Consolas, monospace';
  ctx.strokeStyle = '#2a4147'; ctx.fillStyle = '#829b9d'; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = top + i * (h - top - bottom) / 4;
    ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(w - right, y); ctx.stroke();
    ctx.textAlign = 'right'; ctx.fillText((max - i * (max - min) / 4).toFixed(Math.abs(max - min) > 40 ? 0 : 1), left - 8, y + 3);
  }
  if (min < 0 && max > 0) {   // the zero line
    ctx.strokeStyle = '#80cbd2'; ctx.setLineDash([4, 4]);
    ctx.beginPath(); ctx.moveTo(left, yp(0)); ctx.lineTo(w - right, yp(0)); ctx.stroke(); ctx.setLineDash([]);
  }
  ctx.textAlign = 'left'; ctx.fillStyle = '#829b9d';
  ctx.fillText('60 s ago', left, h - 9); ctx.textAlign = 'right'; ctx.fillText('now', w - right, h - 9);
  ctx.strokeStyle = '#a8e071'; ctx.lineWidth = 2.5; ctx.lineJoin = 'round';
  ctx.beginPath();
  points.forEach((p, i) => i ? ctx.lineTo(xp(p.x), yp(p.y)) : ctx.moveTo(xp(p.x), yp(p.y)));
  ctx.stroke();
  const tip = points[points.length - 1];
  ctx.fillStyle = '#a8e071'; ctx.beginPath(); ctx.arc(xp(tip.x), yp(tip.y), 4, 0, Math.PI * 2); ctx.fill();
}

function drawChart() {
  const canvas = $('trend-chart');
  const box = canvas.getBoundingClientRect();
  if (!box.width || !box.height) return;
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.round(box.width * scale);
  canvas.height = Math.round(box.height * scale);
  const ctx = canvas.getContext('2d');
  ctx.scale(scale, scale);
  const key = $('chart-metric').value;
  // Rsoil spans ~1 kOhm (wet) to "no conduction" (the fit runs Gs -> 0 and reports 1e14+ Ohm in
  // air), so plot it on a log axis clamped at RSOIL_OPEN_OHM. Fits at <= 1 Ohm are degenerate
  // (Gs unbounded, uncertainty in the millions of %) and are dropped rather than drawn as "wet".
  const logAxis = key === 'rsoil_ohm';
  const points = state.events.filter(e => e.metrics && Number.isFinite(e.metrics[key])
      && !(logAxis && e.metrics[key] <= 1))
    .map(e => ({ x: new Date(e.time).valueOf(),
      y: logAxis ? Math.log10(Math.min(e.metrics[key], RSOIL_OPEN_OHM)) : e.metrics[key] })).slice(-700);
  $('chart-empty').style.display = points.length ? 'none' : 'grid';
  if (!points.length) return;
  const w = box.width, h = box.height, left = 58, right = 17, top = 20, bottom = 34;
  const values = points.map(p => p.y), min = Math.min(...values), max = Math.max(...values);
  const pad = Math.max((max - min) * .14, Math.abs(max) * .001, .01);
  const y0 = min - pad, y1 = max + pad;
  const x0 = points[0].x, x1 = Math.max(points[points.length - 1].x, x0 + 1000);
  const xp = x => left + (x - x0) / (x1 - x0) * (w - left - right);
  const yp = y => top + (y1 - y) / (y1 - y0) * (h - top - bottom);
  ctx.font = '11px ui-monospace, Consolas, monospace';
  ctx.fillStyle = '#829b9d';
  ctx.strokeStyle = '#2a4147';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = top + i * (h - top - bottom) / 4;
    ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(w - right, y); ctx.stroke();
    const tick = y1 - i * (y1 - y0) / 4;
    ctx.textAlign = 'right';
    ctx.fillText(logAxis ? formatOhms(10 ** tick) : tick.toFixed(y1 - y0 > 100 ? 0 : 2), left - 8, y + 3);
  }
  ctx.textAlign = 'left';
  ctx.fillText(formatTime(new Date(x0).toISOString()), left, h - 11);
  ctx.textAlign = 'right';
  ctx.fillText(formatTime(new Date(x1).toISOString()), w - right, h - 11);
  ctx.strokeStyle = '#a8e071'; ctx.lineWidth = 2; ctx.lineJoin = 'round';
  ctx.beginPath();
  points.forEach((point, index) => index ? ctx.lineTo(xp(point.x), yp(point.y)) : ctx.moveTo(xp(point.x), yp(point.y)));
  ctx.stroke();
  const last = points[points.length - 1];
  ctx.fillStyle = '#a8e071'; ctx.beginPath(); ctx.arc(xp(last.x), yp(last.y), 3.5, 0, Math.PI * 2); ctx.fill();
}

async function refreshSessions() {
  const data = await request('/api/sessions');
  const select = $('session-select');
  select.replaceChildren();
  for (const session of data.sessions) {
    const option = document.createElement('option');
    option.value = session.id;
    option.textContent = `${session.title} · ${new Date(session.created_at).toLocaleDateString()}`;
    select.appendChild(option);
  }
  if (state.sessionId) select.value = state.sessionId;
}

async function send(command) {
  if (!command.trim()) return;
  const result = await act('/api/command', { command });
  if (result) $('command-input').value = '';
}

function wireControls() {
  $('scan-btn').addEventListener('click', async () => {
    $('scan-btn').disabled = true;
    $('scan-btn').textContent = 'Scanning…';
    try {
      const data = await request('/api/scan', {});
      const select = $('board-select');
      select.replaceChildren(new Option('Auto-select the only MSV4 board', ''));
      for (const board of data.boards) select.add(new Option(`${board.name} · ${board.address}`, board.address));
      toast(data.boards.length ? `${data.boards.length} MSV4 board(s) found` : 'No MSV4 bring-up board found nearby');
    } catch (error) { toast(error.message); }
    finally { $('scan-btn').disabled = false; $('scan-btn').textContent = 'Scan'; }
  });
  $('connect-btn').addEventListener('click', () => act('/api/connect',
    { device: $('device-input').value.trim() || $('board-select').value }, 'Connecting…'));
  $('disconnect-btn').addEventListener('click', () => act('/api/disconnect', {}, 'Disconnected'));
  $('command-form').addEventListener('submit', event => { event.preventDefault(); send($('command-input').value); });
  // Every [data-command] button sends its command, wherever it sits. This used to listen on the
  // command console's row only, so the Live response buttons (a separate panel) did nothing.
  document.addEventListener('click', event => {
    const button = event.target.closest('[data-command]');
    if (button && !button.disabled) send(button.dataset.command);
  });
  $('capture-btn').addEventListener('click', async () => {
    if (state.capturing) { await act('/api/capture/stop', {}, 'Capture stopped'); return; }
    const commands = [...document.querySelectorAll('input[name="capture"]:checked')].map(input => input.value);
    await act('/api/capture/start', { commands, interval: Number($('capture-interval').value) }, 'Capture started');
  });
  $('new-session-btn').addEventListener('click', async () => {
    const result = await act('/api/sessions/new', { title: $('session-title').value });
    if (result) { $('session-title').value = ''; await refreshSessions(); }
  });
  $('open-session-btn').addEventListener('click', async () => {
    const result = await act('/api/sessions/activate', { id: $('session-select').value });
    if (result) toast(`Opened ${result.session.title}`);
  });
  $('export-btn').addEventListener('click', () => {
    const anchor = document.createElement('a');
    anchor.href = `/api/export?id=${encodeURIComponent(state.sessionId)}`;
    document.body.appendChild(anchor); anchor.click(); anchor.remove();
  });
  $('import-btn').addEventListener('click', () => $('import-file').click());
  $('import-file').addEventListener('change', async event => {
    const file = event.target.files[0];
    if (!file) return;
    try {
      const result = await request('/api/sessions/import', JSON.parse(await file.text()));
      await refreshSessions(); await poll(); toast(`Imported ${result.session.title}`);
    } catch (error) { toast(`Import failed: ${error.message}`); }
    event.target.value = '';
  });
  $('note-form').addEventListener('submit', async event => {
    event.preventDefault();
    const value = $('note-input').value.trim();
    if (value && await act('/api/note', { text: value })) $('note-input').value = '';
  });
  $('live-zero').addEventListener('click', () => {
    const start = liveStart();
    const last = lastLive(start);
    if (!last) { toast('Start live first'); return; }
    state.zero = { g: last.g, stream: start >= 0 ? state.events[start].seq : 0 };
    renderLive(); toast(`Zero set at ${last.g.toFixed(1)} µS`);
  });
  $('chart-metric').addEventListener('change', drawChart);
  window.addEventListener('resize', () => { drawChart(); renderLive(); });
}

async function init() {
  try {
    state.token = (await request('/api/config')).token;
    wireControls();
    await refreshSessions();
    await poll();
    setInterval(poll, 300);
  } catch (error) { toast(`Lab failed to start: ${error.message}`); }
}

init();
