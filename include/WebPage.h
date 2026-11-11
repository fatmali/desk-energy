#pragma once

#include <pgmspace.h>

const char DeskWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#172e39">
<title>Desk focus</title>
<style>
:root{color-scheme:light;--ink:#172e39;--paper:#f6f7f5;--muted:#526874;--accent:#176578;--line:#cbd5d8;--error:#a22d36}
*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.5 system-ui,sans-serif}
main{max-width:620px;margin:auto;padding:28px 22px 50px}header{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
h1{font-size:23px;margin:0;font-weight:650}h2{font-size:19px;margin:0 0 12px}.connection{font-size:13px;color:var(--muted)}
.timer{padding:34px 0 26px}.phase{margin:0;color:var(--accent);font-weight:600}.countdown{font-size:clamp(64px,18vw,108px);line-height:1.15;letter-spacing:-.06em;font-variant-numeric:tabular-nums;margin:6px 0}
progress{display:block;width:100%;height:8px;accent-color:var(--accent);margin:18px 0}.meta{color:var(--muted);font-size:14px}
.controls{display:flex;gap:10px;flex-wrap:wrap}button,input{font:inherit}button{border:1px solid var(--ink);background:var(--ink);color:white;min-height:46px;padding:9px 18px;border-radius:7px;cursor:pointer}
button.secondary{color:var(--ink);background:transparent}button:disabled{opacity:.45;cursor:default}button:focus-visible,input:focus-visible,summary:focus-visible{outline:3px solid #df9c38;outline-offset:3px}
section,details{padding:23px 0;border-top:1px solid var(--line)}label{display:block;margin:12px 0 5px}input{width:100%;padding:11px;border:1px solid #8a9ca5;border-radius:6px;background:white;color:var(--ink)}
form button{margin-top:13px}p{margin:9px 0}.job-state{font-size:22px;font-weight:650}.hint{font-size:14px;color:var(--muted)}summary{cursor:pointer;font-weight:600;min-height:32px}
#notice{padding:12px 0;color:var(--accent);min-height:24px}#notice.error{color:var(--error)}[hidden]{display:none!important}.address{overflow-wrap:anywhere}code{font-size:13px}
</style>
</head>
<body><main>
<header><h1>Desk focus</h1><span id="connection" class="connection">Connecting...</span></header>
<div id="notice" role="status" aria-live="polite"></div>
<div class="timer">
  <p id="phase" class="phase">Your next focus</p>
  <div id="countdown" class="countdown" aria-label="Time remaining">--:--</div>
  <progress id="progress" max="1" value="0" aria-label="Session progress"></progress>
  <p id="summary" class="meta">Reading timer...</p>
  <div class="controls">
    <button id="start" data-command="start" disabled>Start focus</button>
    <button id="pause" class="secondary" data-command="pause" disabled>Pause</button>
    <button id="reset" class="secondary" data-command="reset" disabled>Reset</button>
    <button id="skip" class="secondary" data-command="skip" disabled>Skip</button>
  </div>
  <p class="hint">On the device: tap BOOT to start/pause/resume; hold 1.5 seconds to reset. EN/RST restarts the board.</p>
</div>
<section>
  <h2>One thing to work on</h2>
  <form id="task-form">
    <label for="task">Current task</label>
    <input id="task" maxlength="48" autocomplete="off" placeholder="What would make this session useful?">
    <p class="hint">Up to 48 basic English characters. Tasks and build/test details stay on this page, not the clean timer screen.</p>
    <button type="submit">Set task</button>
  </form>
</section>
<section>
  <h2>Latest build or test</h2>
  <p id="job-state" class="job-state">No report yet</p>
  <p id="job-label" class="hint">The optional computer command wrapper sends results here.</p>
</section>
<details id="wifi-panel">
  <summary>Wi-Fi setup</summary>
  <p class="hint">Use a 2.4 GHz network. Credentials stay on this ESP32, not in this page or the project.</p>
  <p id="network-message" class="hint"></p>
  <form id="wifi-form">
    <label for="ssid">Network name</label><input id="ssid" required maxlength="32" autocomplete="off">
    <label for="wifi-password">Wi-Fi password</label><input id="wifi-password" type="password" maxlength="63" autocomplete="new-password">
    <p class="hint">Leave blank only for an open network. Enterprise Wi-Fi and captive-portal networks are not supported.</p>
    <button type="submit">Connect</button>
  </form>
</details>
<details>
  <summary>Device &amp; wireless updates</summary>
  <p id="address" class="hint address"></p>
  <p class="hint">Browser login: <strong>focus</strong>. Use your existing access code. It stays hidden on the timer screen unless requested here.</p>
  <button id="pair" class="secondary">Show access code on screen</button>
  <p class="hint">Use only on a trusted local network. This local HTTP page is not encrypted; never expose it to the internet.</p>
  <form id="update-form">
    <label for="firmware">Firmware update (.bin)</label><input id="firmware" type="file" accept=".bin" required>
    <p class="hint">Upload this project's firmware.bin, not a bootloader or merged image. The timer must be reset to Ready first. An update reboots the device; saved daily counts and Wi-Fi settings remain.</p>
    <button type="submit" class="secondary">Upload firmware</button>
  </form>
</details>
</main>
<script>
const $ = id => document.getElementById(id);
let busy = false, updating = false, lastState = null, clockSyncedAt = 0, receivedTask = false, wifiSubmitted = false, connectionLost = false;
function notice(text, error = false) { $('notice').textContent = text; $('notice').className = error ? 'error' : ''; }
async function request(path, body) {
  const options = {cache:'no-store', credentials:'same-origin', signal:AbortSignal.timeout(6000)};
  if (body !== undefined) {
    options.method = 'POST';
    options.headers = {'Content-Type':'application/json','X-Desk-Focus':'1'};
    options.body = JSON.stringify(body);
  }
  const response = await fetch(path, options);
  if (!response.ok) {
    let message = `Device returned ${response.status}.`;
    try { message = (await response.json()).error || message; } catch { /* The server can return a plain-text authentication error. */ }
    throw new Error(message);
  }
  return response.json();
}
function controls(online) {
  document.querySelectorAll('[data-command]').forEach(button => button.disabled = !online || busy);
  if (online && lastState) {
    $('start').disabled = busy || lastState.state === 'running';
    $('pause').disabled = busy || lastState.state !== 'running';
  }
}
function render(data) {
  lastState = data;
  const phase = {focus:'Focus',short_break:'Short break',long_break:'Long break'}[data.phase];
  $('phase').textContent = phase;
  $('countdown').textContent = `${String(Math.floor(data.remaining / 60)).padStart(2,'0')}:${String(data.remaining % 60).padStart(2,'0')}`;
  $('summary').textContent = `${data.state} / ${data.completed_today === null ? 'waiting for clock sync' : `${data.completed_today} focus sessions today`}`;
  $('progress').value = 1 - data.remaining / data.duration;
  $('start').textContent = data.state === 'paused' ? 'Resume' : `Start ${phase.toLowerCase()}`;
  $('connection').textContent = data.connected ? 'Connected to device' : 'Setup network';
  $('network-message').textContent = data.network_message;
  $('address').textContent = `http://${data.hostname}.local/ - ${data.ip}`;
  if (!receivedTask) { $('task').value = data.task; receivedTask = true; }
  if (!data.connected) $('wifi-panel').open = true;
  const jobNames = {none:'No report yet',running:'Running',passed:'Passed',failed:'Failed',cancelled:'Cancelled',unknown:'Connection lost'};
  $('job-state').textContent = jobNames[data.job.state];
  $('job-label').textContent = data.job.state === 'none' ? 'The optional computer command wrapper sends results here.' :
    `${data.job.kind}: ${data.job.label}${data.job.state === 'unknown' ? ' - no heartbeat for 60 seconds; result unknown' : ''}`;
  controls(true);
}
async function refresh() {
  if (updating) return;
  try {
    render(await request('/api/status'));
    if (connectionLost) { notice('Reconnected to the timer.'); connectionLost = false; }
    if (Date.now() - clockSyncedAt > 60000) {
      await request('/api/clock', {epoch:Math.floor(Date.now()/1000),offset:-new Date().getTimezoneOffset()*60});
      clockSyncedAt = Date.now();
    }
  } catch (error) {
    $('connection').textContent = 'Device unavailable';
    connectionLost = true;
    controls(false);
    notice(wifiSubmitted ? 'If setup succeeded, reconnect to your usual Wi-Fi and open the device hostname shown under Device & wireless updates.' : `${error.message} Check that your phone and ESP32 are on the same network.`, true);
  }
}
async function act(action, success) {
  if (busy) return;
  busy = true; controls(true);
  try { await action(); notice(success); await refresh(); }
  catch (error) { notice(error.message, true); }
  finally { busy = false; controls(lastState !== null && $('connection').textContent !== 'Device unavailable'); }
}
document.querySelectorAll('[data-command]').forEach(button => button.addEventListener('click', () => {
  if (button.dataset.command === 'reset' && !confirm('Reset the timer? Today\'s completed sessions will be kept.')) return;
  if (button.dataset.command === 'skip' && !confirm('Skip this phase? An unfinished focus will not count.')) return;
  act(() => request('/api/control', {action:button.dataset.command}), 'Timer updated.');
}));
$('task-form').addEventListener('submit', event => {
  event.preventDefault();
  const task = $('task').value.trim();
  if (!/^[\x20-\x7e]{0,48}$/.test(task)) { notice('Use up to 48 basic English characters.', true); return; }
  act(() => request('/api/task', {task}), 'Task saved.');
});
$('wifi-form').addEventListener('submit', event => {
  event.preventDefault();
  const ssid = $('ssid').value, password = $('wifi-password').value;
  act(async () => { await request('/api/wifi', {ssid,password}); $('wifi-password').value = ''; wifiSubmitted = true; },
      'Connecting. Reconnect your phone to your usual Wi-Fi, then open the device hostname under Device & wireless updates.');
});
$('pair').addEventListener('click', () => act(() => request('/api/pair', {}), 'Access code is visible on the screen for two minutes.'));
$('update-form').addEventListener('submit', async event => {
  event.preventDefault();
  if (busy || updating) return;
  if (!lastState || lastState.state !== 'ready') { notice('Reset the timer to Ready before updating.', true); return; }
  if (!confirm('Install this firmware and reboot the timer?')) return;
  const file = $('firmware').files[0];
  if (!file || !file.name.endsWith('.bin') || file.size > 6553600) { notice('Select a firmware.bin no larger than 6.25 MiB.', true); return; }
  const form = new FormData(); form.append('firmware', file);
  updating = true; busy = true; controls(false); notice('Uploading firmware. Keep the ESP32 powered on.');
  try {
    const response = await fetch('/api/update', {method:'POST',headers:{'X-Desk-Focus':'1'},body:form,signal:AbortSignal.timeout(120000)});
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Firmware update failed.');
    notice('Update accepted. Rebooting; this page will reload in 8 seconds.');
    setTimeout(() => location.reload(), 8000);
  } catch (error) { updating = false; busy = false; notice(error.message, true); }
});
async function poll() { await refresh(); setTimeout(poll, 2000); }
poll();
</script></body></html>)HTML";
