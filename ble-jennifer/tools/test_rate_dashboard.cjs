// Run Zack's unchanged dashboard and the isolated rate add-on with stand-ins.
// No browser or physical BLE device is driven by these checks.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const scriptPath = path.join(root, 'dashboard/dashboard/static/js/dashboard.js');
const originalBytes = fs.readFileSync(scriptPath);
const source = originalBytes.toString('utf8');
const rateSource = fs.readFileSync(path.join(root, 'dashboard/dashboard/static/js/telemetry-rate.js'), 'utf8');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'upstream-manifest.json'), 'utf8'));
let assertions = 0;
const equal = (actual, expected, message) => { assert.equal(actual, expected, message); assertions++; };
const near = (actual, expected, message) => { assert(Math.abs(actual - expected) < 1e-8, message); assertions++; };
const entry = manifest.dashboard.files.find(f => f.path === 'dashboard/static/js/dashboard.js');
const blobHash = crypto.createHash('sha1').update(Buffer.from('blob ' + originalBytes.length + '\0')).update(originalBytes).digest('hex');
equal(blobHash, entry.sha, 'dashboard.js must match Zack\'s exact GitHub blob');

function dashboard(withRate = true) {
    let now = 10000, timerId = 0;
    const timers = new Map(), events = new Map(), elements = new Map(), emitted = [];
    const element = id => {
        if (!elements.has(id)) elements.set(id, {
            innerText: '', title: '', value: '5', disabled: false, dataset: {}, attributes: {}, listeners: {},
            setAttribute(key, value) { this.attributes[key] = value; },
            getAttribute(key) { return this.attributes[key]; },
            addEventListener(key, callback) { this.listeners[key] = callback; },
            getContext() { return {}; },
        });
        return elements.get(id);
    };
    class Clock extends Date { static now() { return now; } }
    const context = {
        Date: Clock, Math, console,
        document: { getElementById: element, documentElement: element('root') },
        window: { requestAnimationFrame() {} },
        getComputedStyle: () => ({ getPropertyValue: () => '#123456' }),
        performance: { now: () => now }, localStorage: { setItem() {} },
        Chart: class { constructor(canvas, config) { Object.assign(this, config); } update() {} },
        io: () => ({ on: (name, callback) => { if (!events.has(name)) events.set(name, []); events.get(name).push(callback); },
                     emit: (name, value) => emitted.push([name, value]) }),
        setTimeout: (callback, delay) => { const id = ++timerId; timers.set(id, { callback, at: now + delay }); return id; },
        clearTimeout: id => timers.delete(id),
    };
    vm.createContext(context);
    vm.runInContext(source, context, { filename: 'dashboard.js' });
    if (withRate) vm.runInContext(rateSource, context, { filename: 'telemetry-rate.js' });
    const api = context.window.__dashboard;
    const emit = (name, value) => { for (const callback of events.get(name) || []) callback(value); };
    const render = () => api.render(now);
    const sample = (values = {}) => { emit('sensor_update', { pitch: 8.5, roll: -4.5, power: 1.25,
        pitch_target: 8, roll_target: -4, ...values }); render(); };
    const advance = ms => { now += ms; for (const [id, timer] of timers) if (timer.at <= now) { timers.delete(id); timer.callback(); } };
    emit('connection_status', { status: 'connected' });
    return { api, element, emit, render, sample, advance, emitted,
        error: () => vm.runInContext('currentError(Date.now())', context) };
}

// A native select only fires change when the user chooses a different value.
function chooseRate(dashboard, value) {
    const selector = dashboard.element('telemetryRate');
    if (selector.disabled || selector.value === value) return false;
    selector.value = value;
    selector.listeners.change();
    return true;
}

// The add-on may not change readings, error calculations, or exported columns.
{
    const baseline = dashboard(false), addon = dashboard(true);
    for (const d of [baseline, addon]) {
        d.sample(); d.advance(200); d.sample({ pitch: 10, roll: -2 });
        d.advance(200); d.sample({ pitch_target: null, roll_target: null });
        d.advance(200); d.sample({ pitch: 0, roll: 0, pitch_target: 0, roll_target: 0 });
    }
    equal(JSON.stringify(addon.api.state()), JSON.stringify(baseline.api.state()), 'rate script must not mutate original sensor state');
    equal(addon.api.csvFromRows(addon.api.state().csvRows), baseline.api.csvFromRows(baseline.api.state().csvRows));
    equal(addon.api.csvFromRows([]).trim(), 'time_iso,elapsed_s,run,pitch_deg,roll_deg,power_w,pitch_target_deg,roll_target_deg');
    equal(addon.api.state().csvRows.every(row => row.length === 7), true, 'no additional CSV metadata');
}
{
    const d = dashboard(); d.sample();
    near(d.error().pitch, 0.5); near(d.error().roll, -0.5);
    equal(d.element('levelTitle').innerText, 'Error from target');
    equal(d.element('rmsLabel').innerText, 'RMS error');
    d.advance(200); d.sample({ pitch: 10, roll: -2 });
    equal(d.element('withinBand').innerText, '50%', 'preserve Zack\'s combined-magnitude band rather than separate per-axis threshold');
    d.sample({ pitch_target: null, roll_target: null });
    equal(d.element('levelTitle').innerText, 'Tilt from level', 'preserve Zack\'s no-target fallback');
    near(d.error().pitch, 8.5); near(d.error().roll, -4.5);
    d.sample({ pitch: 0, roll: 0, pitch_target: 0, roll_target: 0 });
    equal(d.element('levelTitle').innerText, 'Error from target', 'numeric zero is an existing valid target');
    near(d.error().pitch, 0); near(d.error().roll, 0);
    d.sample({ pitch: null }); equal(d.error(), null);
}
{
    const d = dashboard(), select = d.element('telemetryRate'), status = d.element('telemetryRateStatus');
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 5, available: true, confirmed_at: 10 });
    equal(select.value, '5'); equal(select.disabled, false);
    equal(status.innerText, 'ESP32 rate: 5 updates/s');
    select.value = '2'; select.listeners.change();
    equal(status.dataset.state, 'applying', 'selection has immediate feedback before a server roundtrip');
    equal(status.innerText, 'Applying 2 updates/s...'); equal(select.disabled, true);
    equal(status.getAttribute('aria-busy'), 'true');
    equal(JSON.stringify(d.emitted[0]), JSON.stringify(['set_telemetry_rate', { rate_hz: 2 }]));
    d.emit('telemetry_rate', { state: 'pending', requested_hz: 2, available: true });
    equal(status.dataset.state, 'applying');
    d.emit('telemetry_rate', { state: 'verifying', requested_hz: 2, available: true });
    equal(status.dataset.state, 'verifying');
    equal(status.innerText, 'Verifying 2 updates/s with ESP32...');
    d.sample(); equal(d.api.state().csvRows.length, 1, 'sensor readings continue while changing rate');
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 2, available: true, confirmed_at: 10 });
    equal(status.dataset.state, 'verifying', 'brief progress is retained for readability');
    d.advance(450);
    equal(status.dataset.state, 'confirmed'); equal(status.innerText, 'ESP32 rate: 2 updates/s');
    equal(status.getAttribute('aria-busy'), 'false'); equal(select.disabled, false);
    equal(status.title.startsWith('Last read back from the ESP32 at'), true);
    equal(d.api.state().csvRows.length, 1, 'confirmation is not recorded as a sensor reading');
    select.value = '10'; select.listeners.change();
    d.emit('telemetry_rate', { state: 'error', available: true, error: 'Firmware applied 2 Hz instead of 10 Hz' });
    d.advance(500);
    equal(status.dataset.state, 'error'); equal(select.value, '2', 'failed change restores last applied selection');
    equal(status.innerText, 'Firmware applied 2 Hz instead of 10 Hz'); equal(select.disabled, false);
}
{
    const d = dashboard(), select = d.element('telemetryRate'), status = d.element('telemetryRateStatus');
    select.value = '10'; select.listeners.change();
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 10, available: true });
    d.emit('disconnect'); d.advance(500); d.render();
    equal(status.dataset.state, 'disconnected', 'dashboard disconnect cancels delayed success');
    equal(select.disabled, true);
    equal(status.innerText, 'Dashboard disconnected; reconnecting...');
    equal(d.element('connectionStatus').innerText, 'Reconnecting...', 'original disconnect listener is retained');
    d.emit('telemetry_rate', { state: 'unsupported', available: false });
    equal(status.dataset.state, 'unsupported'); equal(status.innerText, 'Firmware has no rate control');
    equal(select.disabled, true);
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 10, available: true });
    equal(status.dataset.state, 'confirmed'); equal(select.disabled, false);
    select.value = '1'; select.listeners.change();
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 1, available: true });
    d.emit('telemetry_rate', { state: 'disconnected', available: false });
    d.advance(500);
    equal(status.dataset.state, 'disconnected', 'BLE loss cancels delayed success');
    equal(status.innerText, 'Connect BLE to change rate'); equal(select.disabled, true);
}
// A subsequent request, including the same rate from another browser, must
// cancel an earlier delayed success while retaining its own feedback duration.
for (const nextRate of [2, 10]) {
    const d = dashboard(), select = d.element('telemetryRate'), status = d.element('telemetryRateStatus');
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 5, available: true });
    select.value = '2'; select.listeners.change();
    d.emit('telemetry_rate', { state: 'pending', requested_hz: 2, available: true });
    d.emit('telemetry_rate', { state: 'verifying', requested_hz: 2, available: true });
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 2, available: true });
    d.advance(100);
    d.emit('telemetry_rate', { state: 'pending', requested_hz: nextRate, available: true });
    equal(status.innerText, 'Applying ' + nextRate + ' updates/s...');
    d.emit('telemetry_rate', { state: 'verifying', requested_hz: nextRate, available: true });
    d.advance(350); // The previous request's delayed success would fire now.
    equal(status.dataset.state, 'verifying', 'old success cannot overwrite a new request');
    equal(status.innerText, 'Verifying ' + nextRate + ' updates/s with ESP32...');
    equal(select.disabled, true);
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: nextRate, available: true });
    d.advance(99);
    equal(status.dataset.state, 'verifying', 'new request keeps its own feedback interval');
    d.advance(1);
    equal(status.dataset.state, 'confirmed');
    equal(status.innerText, 'ESP32 rate: ' + nextRate + ' updates/s');
    equal(select.disabled, false);
    equal(select.value, String(nextRate));
}
// Initial read retries stay visible and never stop sensor delivery or allow a
// delayed success from an earlier connection to hide the current check.
{
    const d = dashboard(), select = d.element('telemetryRate'), status = d.element('telemetryRateStatus');
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 5, available: true });
    select.value = '2'; select.listeners.change();
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 2, available: true });
    d.emit('telemetry_rate', { state: 'checking', attempt: 1, max_attempts: 3, available: false });
    d.advance(500);
    equal(status.dataset.state, 'checking', 'checking cancels an earlier delayed success');
    equal(status.innerText, 'Checking ESP32 rate (1/3)...');
    equal(select.disabled, true);
    equal(status.getAttribute('aria-busy'), 'true');
    d.sample();
    equal(d.api.state().csvRows.length, 1, 'readings continue during initial rate checks');
    d.emit('telemetry_rate', { state: 'checking', attempt: 2, max_attempts: 3, available: false });
    equal(status.innerText, 'Checking ESP32 rate (2/3)...');
    d.emit('telemetry_rate', { state: 'checking', attempt: 3, max_attempts: 3, available: false });
    equal(status.innerText, 'Checking ESP32 rate (3/3)...');
    d.emit('telemetry_rate', { state: 'error', available: false,
        error: 'Could not check ESP32 rate after 3 attempts. Readings continue; reconnect Bluetooth to try again.' });
    d.advance(500);
    equal(status.dataset.state, 'error');
    equal(select.disabled, true);
    equal(status.getAttribute('aria-busy'), 'false');
    equal(status.innerText.includes('after 3 attempts'), true);
    equal(d.api.state().csvRows.length, 1, 'checking/error statuses are not CSV readings');
    d.emit('telemetry_rate', { state: 'checking', attempt: 1, max_attempts: 3, available: false });
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 5, available: true });
    equal(status.dataset.state, 'confirmed');
    equal(status.innerText, 'ESP32 rate: 5 updates/s');
    equal(select.disabled, false);
    d.emit('telemetry_rate', { state: 'checking', attempt: 1, max_attempts: 3, available: false });
    d.emit('telemetry_rate', { state: 'unsupported', available: false });
    equal(status.dataset.state, 'unsupported');
    equal(status.innerText, 'Firmware has no rate control');
    equal(select.disabled, true);
    equal(status.getAttribute('aria-busy'), 'false');
}
{
    const d = dashboard(), select = d.element('telemetryRate'), status = d.element('telemetryRateStatus');
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 2, available: true });
    equal(chooseRate(d, '2'), false, 'choosing an already selected value does not fire change');
    equal(d.emitted.length, 0);
    d.emit('telemetry_rate', { state: 'disconnected', available: false });
    d.emit('telemetry_rate', { state: 'checking', attempt: 1, max_attempts: 3, available: false });
    d.emit('telemetry_rate', { state: 'pending', requested_hz: 2, available: true });
    d.emit('telemetry_rate', { state: 'verifying', requested_hz: 2, available: true });
    d.emit('telemetry_rate', { state: 'error', available: true, error: 'Could not confirm rate' });
    equal(select.value, '', 'an unconfirmed restore must allow the same rate to be chosen again');
    equal(select.disabled, false);
    equal(status.dataset.state, 'error');
    equal(status.innerText, 'Could not confirm rate');
    const template = fs.readFileSync(path.join(root, 'dashboard/dashboard/templates/index.html'), 'utf8');
    const rateSelect = template.match(/<select\b[^>]*id="telemetryRate"[^>]*>([\s\S]*?)<\/select>/)[1];
    const placeholder = rateSelect.match(/<option\b([^>]*\bvalue=""[^>]*)>([^<]*)<\/option>/);
    equal(placeholder && placeholder[2].trim(), 'Choose a rate', 'the empty selection needs a visible label');
    equal(/\bdisabled\b/.test(placeholder[1]), true, 'the prompt is not a sending rate');
    // Even a synthetic change on the prompt must not send an invalid rate.
    select.listeners.change();
    equal(d.emitted.length, 0);
    equal(select.disabled, false);
    equal(status.dataset.state, 'error');
    d.sample();
    equal(d.api.state().csvRows.length, 1, 'readings continue while the user can retry');
    equal(chooseRate(d, '2'), true, 'retry the same rate without an intermediate setting');
    equal(d.emitted.length, 1);
    equal(JSON.stringify(d.emitted[0]), JSON.stringify(['set_telemetry_rate', { rate_hz: 2 }]));
    equal(status.innerText, 'Applying 2 updates/s...');
    equal(select.disabled, true);
    d.emit('telemetry_rate', { state: 'pending', requested_hz: 2, available: true });
    d.emit('telemetry_rate', { state: 'verifying', requested_hz: 2, available: true });
    equal(status.dataset.state, 'verifying');
    d.emit('telemetry_rate', { state: 'confirmed', rate_hz: 2, available: true });
    d.advance(450);
    equal(status.dataset.state, 'confirmed');
    equal(select.value, '2');
    equal(select.disabled, false);
    equal(d.api.state().csvRows.length, 1, 'retry feedback does not become sensor data');
}
console.log(JSON.stringify({ passed: true, assertions,
    scope: 'Zack dashboard Git blob equality and original behavior preserved; isolated rate add-on in Node VM, no browser/device' }, null, 2));
