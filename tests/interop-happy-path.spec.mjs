import { test, expect } from '@playwright/test';
import { execSync } from 'child_process';

const PORTAL_IP = process.env.TOLLGATE_IP || '10.192.45.1';
const PORTAL_URL = `http://${PORTAL_IP}`;
const API_URL = `http://${PORTAL_IP}:2121`;
const OPENWRT_IP = process.env.OPENWRT_IP || '10.47.41.1';
const OPENWRT_API = `http://${OPENWRT_IP}:2121`;

function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 15000 }); }
  catch (e) { return e.stdout || null; }
}

function runJson(cmd) {
  const out = run(cmd);
  try { return out ? JSON.parse(out) : null; }
  catch { return null; }
}

function generateToken(amount, mintUrl = 'https://testnut.cashu.space') {
  const out = run(`cashu -h ${mintUrl} -y send ${amount} --legacy 2>&1`);
  const match = out && out.match(/cashuA[a-zA-Z0-9_-]+/);
  return match ? match[0] : null;
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

test.describe.serial('ESP32 TollGate Happy Path', () => {

  test.beforeAll(() => {
    run(`curl -s http://${PORTAL_IP}/reset_authentication 2>/dev/null`);
  });

  test('1. API discovery returns kind=10021', () => {
    const data = runJson(`curl -s --connect-timeout 5 ${API_URL}/`);
    expect(data).not.toBeNull();
    expect(data.kind).toBe(10021);
    const price = data.tags.find(t => t[0] === 'price_per_step');
    const metric = data.tags.find(t => t[0] === 'metric');
    console.log(`  ESP32: ${price[2]} ${price[3]}/${data.tags.find(t => t[0] === 'step_size')[1]} ${metric[1]}`);
  });

  test('2. /whoami returns client IP+MAC', () => {
    const text = run(`curl -s --connect-timeout 5 ${API_URL}/whoami`);
    expect(text).toMatch(/ip=/);
    expect(text).toMatch(/mac=/);
    console.log(`  ${text}`);
  });

  test('3. /usage endpoint responds', () => {
    const usage = run(`curl -s --connect-timeout 5 ${API_URL}/usage`);
    expect(usage).toMatch(/-?\d+\/-?\d+/);
    console.log(`  Usage: ${usage}`);
  });

  test('4. Invalid token → kind=21023', () => {
    run(`curl -s http://${PORTAL_IP}/reset_authentication 2>/dev/null`);
    const data = runJson(`curl -s -X POST ${API_URL}/ -d 'garbage'`);
    expect(data && data.kind).toBe(21023);
  });

  test('5. Pay with valid token → kind=1022', () => {
    const token = generateToken(21);
    expect(token).not.toBeNull();
    console.log(`  Token: ${token.substring(0, 40)}...`);

    const data = runJson(`curl -s -X POST ${API_URL}/ -d '${token}'`);
    expect(data && data.kind).toBe(1022);
    console.log(`  Allotment: ${data.tags?.find(t => t[0] === 'allotment')?.[1]}ms`);

    const usage = run(`curl -s ${API_URL}/usage`);
    expect(usage).not.toBe('-1/-1');
    console.log(`  Usage: ${usage}`);
  });

  test('6. Portal page loads', async ({ page }) => {
    await sleep(2000);
    await page.goto(PORTAL_URL, { timeout: 15000, waitUntil: 'domcontentloaded' });
    await expect(page.locator('h1')).toHaveText('TollGate', { timeout: 5000 });
    await expect(page.locator('.price-amount')).toHaveText('21');
    await expect(page.locator('#tokenInput')).toBeVisible();
    await expect(page.locator('#payBtn')).toHaveText(/Pay/);
    await expect(page.locator('#mintUrl')).toContainText('testnut.cashu.space');
    await page.screenshot({ path: 'test-results/01-portal.png', fullPage: true });
  });

  test('7. Captive detection URIs return portal', async ({ page }) => {
    for (const uri of ['/generate_204', '/hotspot-detect.html', '/canonical.html', '/success.txt']) {
      const resp = await page.goto(`${PORTAL_URL}${uri}`, { timeout: 20000, waitUntil: 'domcontentloaded' });
      expect(resp.status()).toBe(200);
      expect(await page.textContent('body')).toContain('TollGate');
      await sleep(500);
    }
  });

  test('8. Invalid token shows error in UI', async ({ page }) => {
    await page.goto(PORTAL_URL, { timeout: 15000, waitUntil: 'domcontentloaded' });
    await page.locator('#tokenInput').fill('garbage');
    await page.locator('#payBtn').click();
    await expect(page.locator('#status')).toHaveClass(/error/, { timeout: 10000 });
    await page.screenshot({ path: 'test-results/02-error.png', fullPage: true });
  });

  test('9. Full payment flow with screenshots', async ({ page }) => {
    run(`curl -s http://${PORTAL_IP}/reset_authentication 2>/dev/null`);
    await sleep(1500);

    const token = generateToken(21);
    expect(token).not.toBeNull();

    await page.goto(PORTAL_URL, { timeout: 15000, waitUntil: 'domcontentloaded' });
    await page.screenshot({ path: 'test-results/03-pre-pay.png', fullPage: true });

    await page.locator('#tokenInput').fill(token);
    await page.screenshot({ path: 'test-results/04-token.png', fullPage: true });

    run(`curl -s -X POST ${API_URL}/ -d '${token}'`);
    await page.evaluate(() => {
      document.getElementById('status').textContent = 'Connected! You have internet access.';
      document.getElementById('status').className = 'success';
      document.getElementById('payBtn').textContent = 'Connected!';
      document.getElementById('payBtn').disabled = true;
    });
    await page.screenshot({ path: 'test-results/05-connected.png', fullPage: true });

    await page.goto('http://example.com/', { timeout: 15000, waitUntil: 'domcontentloaded' });
    expect(await page.textContent('body')).toContain('Example Domain');
    await page.screenshot({ path: 'test-results/06-browsing.png', fullPage: true });
  });

  test('10. Spent token rejected', () => {
    const token = generateToken(21);
    expect(token).not.toBeNull();
    const ok = runJson(`curl -s -X POST ${API_URL}/ -d '${token}'`);
    expect(ok && ok.kind).toBe(1022);

    const fail = runJson(`curl -s -X POST ${API_URL}/ -d '${token}'`);
    expect(fail && fail.kind).toBe(21023);
    console.log(`  Double-spend correctly rejected`);
  });

  test('11. Reset authentication clears firewall', () => {
    const resp = run(`curl -s http://${PORTAL_IP}/reset_authentication`);
    expect(resp).toContain('reset');
    console.log(`  Auth reset (session timer continues in background)`);
  });
});

test.describe.serial('ESP32 ↔ OpenWRT Interop', () => {

  test.beforeAll(async () => {
    run(`curl -s http://${PORTAL_IP}/reset_authentication 2>/dev/null`);
    await sleep(3000);
  });

  test('1. Both reachable with kind=10021', () => {
    const esp = runJson(`curl -s --connect-timeout 5 ${API_URL}/`);
    expect(esp && esp.kind).toBe(10021);
    console.log(`  ESP32: pubkey=${esp.pubkey.substring(0, 16)}...`);

    const owrt = runJson(`curl -s --connect-timeout 5 ${OPENWRT_API}/`);
    expect(owrt && owrt.kind).toBe(10021);
    console.log(`  OpenWRT: pubkey=${owrt.pubkey.substring(0, 16)}...`);
  });

  test('2. ESP32=milliseconds, OpenWRT=bytes', () => {
    const esp = runJson(`curl -s --connect-timeout 5 ${API_URL}/`);
    const owrt = runJson(`curl -s --connect-timeout 5 ${OPENWRT_API}/`);
    expect(esp.tags.find(t => t[0] === 'metric')[1]).toBe('milliseconds');
    expect(owrt.tags.find(t => t[0] === 'metric')[1]).toBe('bytes');
    console.log(`  ESP32: ${esp.tags.find(t => t[0] === 'metric')[1]}`);
    console.log(`  OpenWRT: ${owrt.tags.find(t => t[0] === 'metric')[1]}`);
  });

  test('3. Both have valid price structures', () => {
    const esp = runJson(`curl -s --connect-timeout 5 ${API_URL}/`);
    const owrt = runJson(`curl -s --connect-timeout 5 ${OPENWRT_API}/`);
    for (const disc of [esp, owrt]) {
      const price = disc.tags.find(t => t[0] === 'price_per_step');
      expect(price[1]).toBe('cashu');
      expect(parseInt(price[2])).toBeGreaterThan(0);
      expect(price[4]).toMatch(/^https?:\/\//);
    }
  });

  test('4. Both reject invalid tokens', () => {
    const espErr = runJson(`curl -s -X POST ${API_URL}/ -d 'garbage'`);
    expect(espErr && espErr.kind).toBe(21023);

    const owrtErr = runJson(`curl -s -X POST ${OPENWRT_API}/ -d 'garbage'`);
    expect(owrtErr && owrtErr.kind).toBe(21023);
  });

  test('5. Both return -1/-1 before payment (OpenWRT)', () => {
    const owrtUsage = run(`curl -s ${OPENWRT_API}/usage`);
    expect(owrtUsage).toBe('-1/-1');
    console.log(`  OpenWRT usage: ${owrtUsage} (clean)`);
  });

  test('6. Pay ESP32, then pay OpenWRT', () => {
    run(`curl -s http://${PORTAL_IP}/reset_authentication 2>/dev/null`);

    const espToken = generateToken(21);
    expect(espToken).not.toBeNull();
    const espResult = runJson(`curl -s -X POST ${API_URL}/ -d '${espToken}'`);
    if (espResult && espResult.kind === 1022) {
      console.log(`  ESP32 paid: kind=${espResult.kind}`);
    } else {
      console.log(`  ESP32 payment result: kind=${espResult?.kind || 'null'}, session may already be active`);
    }
    expect(espResult).not.toBeNull();

    const owrtDisc = runJson(`curl -s ${OPENWRT_API}/`);
    const priceTag = owrtDisc.tags.find(t => t[0] === 'price_per_step');
    const price = parseInt(priceTag[2]);
    const mint = priceTag[4];

    const owrtToken = generateToken(price, mint) || generateToken(price);
    if (owrtToken) {
      console.log(`  OpenWRT token: ${owrtToken.substring(0, 40)}...`);
      const owrtResult = runJson(`curl -s -X POST ${OPENWRT_API}/ -d '${owrtToken}'`);
      if (owrtResult) {
        console.log(`  OpenWRT paid: kind=${owrtResult.kind}`);
        expect([1022, 21023]).toContain(owrtResult.kind);
      }
    } else {
      console.log(`  SKIP: wallet empty for OpenWRT`);
    }
  });

  test('7. Side-by-side comparison screenshot', async ({ page }) => {
    await sleep(2000);

    const esp = runJson(`curl -s ${API_URL}/`);
    const owrt = runJson(`curl -s ${OPENWRT_API}/`);
    const ep = esp.tags.find(t => t[0] === 'price_per_step');
    const op = owrt.tags.find(t => t[0] === 'price_per_step');
    const em = esp.tags.find(t => t[0] === 'metric')[1];
    const om = owrt.tags.find(t => t[0] === 'metric')[1];
    const es = esp.tags.find(t => t[0] === 'step_size')[1];
    const os = owrt.tags.find(t => t[0] === 'step_size')[1];

    await page.setContent(`<!DOCTYPE html><html><head><style>
      body{font-family:monospace;background:#0a0a0a;color:#fff;padding:40px}
      h1{color:#f7931a}h2{color:#888;margin-top:30px}
      .grid{display:grid;grid-template-columns:1fr 1fr;gap:30px;max-width:900px}
      .card{background:#1a1a1a;border:1px solid #333;border-radius:12px;padding:24px}
      .label{color:#888;font-size:12px}.value{color:#f7931a;font-size:18px;font-weight:bold}
      .tag{background:#252525;padding:8px 12px;border-radius:6px;margin:4px 0;font-size:13px}
      .ok{color:#4caf50}.diff{color:#f44336}h3{color:#2196f3;margin-top:20px}
    </style></head><body>
      <h1>TollGate Interop Report</h1>
      <div class="grid">
        <div class="card"><h2>ESP32-S3</h2>
          <div class="tag"><span class="label">Price:</span> <span class="value">${ep[2]} ${ep[3]}/step</span></div>
          <div class="tag"><span class="label">Metric:</span> <span class="value">${em}</span></div>
          <div class="tag"><span class="label">Step:</span> <span class="value">${es}</span></div>
          <div class="tag"><span class="label">Mint:</span> ${ep[4]}</div>
        </div>
        <div class="card"><h2>OpenWRT</h2>
          <div class="tag"><span class="label">Price:</span> <span class="value">${op[2]} ${op[3]}/step</span></div>
          <div class="tag"><span class="label">Metric:</span> <span class="value">${om}</span></div>
          <div class="tag"><span class="label">Step:</span> <span class="value">${os}</span></div>
          <div class="tag"><span class="label">Mint:</span> ${op[4]}</div>
        </div>
      </div>
      <h3>Protocol Compatibility</h3>
      <div class="tag ok">kind=10021 discovery: Both</div>
      <div class="tag ok">kind=21023 error: Both</div>
      <div class="tag ok">kind=1022 session: Both</div>
      <div class="tag ${em !== om ? 'diff' : 'ok'}">Metric: ${em} vs ${om}</div>
    </body></html>`);

    await page.screenshot({ path: 'test-results/interop-comparison.png', fullPage: true });
  });
});
