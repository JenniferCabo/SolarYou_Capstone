const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
// Install the test dependency locally with: npm install --no-save playwright
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const artifacts = path.join(root, 'artifacts');
fs.mkdirSync(artifacts, { recursive: true });

(async () => {
    const installedBrowser = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
        'C:/Program Files/Google/Chrome/Application/chrome.exe'].find(candidate => fs.existsSync(candidate));
    const browser = await chromium.launch({ headless: true, ...(installedBrowser ? { executablePath: installedBrowser } : {}) });
    try {
        const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
        const errors = [];
        page.on('pageerror', error => errors.push(String(error)));
        await page.goto('http://127.0.0.1:5012', { waitUntil: 'networkidle' });
        await page.waitForFunction(() => document.getElementById('pitchValue').innerText !== '--');
        await page.waitForFunction(() => !document.getElementById('telemetryRate').disabled);
        assert.match(await page.locator('body').innerText(), /SIMULATED BLE/);
        assert.equal(await page.getByRole('heading', { name: 'Panel attitude', exact: true }).count(), 1);
        assert.equal(await page.locator('#targetStatus').count(), 0, 'Custom target-status UI was removed');
        assert.equal(await page.locator('#pitchError').count(), 0, 'Custom axis-error readouts were removed');
        await page.waitForFunction(() => document.getElementById('levelTitle').innerText === 'Error from target', null, { timeout: 30000 });
        assert.equal(await page.locator('#rmsLabel').innerText(), 'RMS error');
        const immediateFeedback = await page.evaluate(() => {
            const select = document.getElementById('telemetryRate');
            select.value = '2';
            select.dispatchEvent(new Event('change', { bubbles: true }));
            return { text: document.getElementById('telemetryRateStatus').innerText,
                state: document.getElementById('telemetryRateStatus').dataset.state, disabled: select.disabled };
        });
        assert.equal(immediateFeedback.state, 'applying');
        assert.match(immediateFeedback.text, /Applying 2 updates\/s/);
        assert.equal(immediateFeedback.disabled, true);
        await page.waitForFunction(() => document.getElementById('telemetryRateStatus').dataset.state === 'verifying');
        await page.screenshot({ path: path.join(artifacts, 'dashboard-verifying.png'), fullPage: true });
        await page.waitForFunction(() => document.getElementById('telemetryRateStatus').innerText === 'ESP32 rate: 2 updates/s');
        await page.selectOption('#telemetryRate', '10');
        await page.waitForFunction(() => document.getElementById('telemetryRateStatus').innerText === 'ESP32 rate: 10 updates/s');
        // Observe an actual simulated drop/reconnect, then check the chosen rate was restored.
        await page.evaluate(() => {
            window.rateStates = [];
            new MutationObserver(() => window.rateStates.push(document.getElementById('telemetryRateStatus').innerText))
                .observe(document.getElementById('telemetryRateStatus'), { childList: true, subtree: true, characterData: true });
        });
        await page.waitForFunction(() => window.rateStates.includes('Connect BLE to change rate'), null, { timeout: 30000 });
        await page.waitForFunction(() => document.getElementById('telemetryRateStatus').innerText === 'ESP32 rate: 10 updates/s', null, { timeout: 10000 });
        assert.match(await page.locator('#telemetryRateStatus').getAttribute('title'), /Last read back from the ESP32 at/);
        const before = await page.evaluate(() => window.__dashboard.state().csvRows.length);
        assert(before > 5);
        await page.screenshot({ path: path.join(artifacts, 'dashboard-desktop.png'), fullPage: true });
        const download = page.waitForEvent('download');
        await page.click('#exportCsv');
        await (await download).saveAs(path.join(artifacts, 'simulation-export.csv'));
        const csv = fs.readFileSync(path.join(artifacts, 'simulation-export.csv'), 'utf8');
        assert.equal(csv.split('\n')[0], 'time_iso,elapsed_s,run,pitch_deg,roll_deg,power_w,pitch_target_deg,roll_target_deg');
        assert(csv.trim().split('\n').every(line => line.split(',').length === 8));
        await page.setViewportSize({ width: 390, height: 844 });
        await page.waitForTimeout(300);
        const overflow = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth + 1);
        assert.equal(overflow, false, 'Dashboard should not overflow the mobile viewport');
        await page.screenshot({ path: path.join(artifacts, 'dashboard-mobile.png'), fullPage: true });
        assert.deepEqual(errors, []);
        const report = { passed: true, checks: ['live simulated C telemetry', 'Zack original dashboard target-error display',
            'custom target status and axis readouts absent',
            'immediate applying feedback and visible verifying state', 'applied-rate readback', 'reconnection and selected-rate restoration', 'original eight-column CSV export',
            'mobile viewport', 'no browser errors'], recordedRows: before, pageErrors: errors };
        fs.writeFileSync(path.join(artifacts, 'browser-results.json'), JSON.stringify(report, null, 2));
        console.log(JSON.stringify(report, null, 2));
    } finally {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
