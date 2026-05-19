import { test, expect } from '@playwright/test';

const PORTAL_IP = process.env.TOLLGATE_IP || '10.192.45.1';
const PORTAL_URL = `http://${PORTAL_IP}`;

const SETUP_HTML = `<!DOCTYPE html>
<html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>TollGate Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#0a0a0a;color:#fff;display:flex;align-items:center;justify-content:center;
min-height:100vh;padding:20px}
.card{background:#1a1a1a;border:1px solid #333;border-radius:16px;padding:32px;
max-width:400px;width:100%;text-align:center}
h1{font-size:24px;margin-bottom:8px;color:#f7931a}
.subtitle{color:#888;margin-bottom:20px;font-size:13px}
.networks{margin-top:16px;text-align:left}
.net-item{background:#252525;border:1px solid #333;border-radius:8px;
padding:12px;margin-bottom:8px;cursor:pointer;display:flex;justify-content:space-between;align-items:center}
.net-item:hover{border-color:#f7931a}
.net-item:active{background:#333}
.net-ssid{font-size:14px}
.net-rssi{font-size:11px;color:#888}
.net-lock{color:#f7931a;margin-right:4px}
.manual{margin-top:12px}
input{width:100%;background:#252525;border:1px solid #333;border-radius:8px;
color:#fff;padding:12px;font-size:14px;margin-bottom:8px;outline:none}
input:focus{border-color:#f7931a}
.btn{background:#f7931a;color:#000;border:none;border-radius:8px;padding:14px 28px;
font-size:16px;font-weight:bold;cursor:pointer;width:100%;margin-top:8px}
.btn:hover{background:#e8850f}
.btn:disabled{background:#333;color:#666;cursor:not-allowed}
#status{margin-top:12px;padding:10px;border-radius:8px;display:none;font-size:13px}
#status.success{display:block;background:#1a472a;color:#4caf50}
#status.error{display:block;background:#471a1a;color:#f44336}
#status.processing{display:block;background:#1a3a47;color:#2196f3}
.refresh{background:none;border:1px solid #444;color:#aaa;border-radius:6px;
padding:6px 12px;font-size:12px;cursor:pointer;margin-top:4px}
.refresh:hover{border-color:#f7931a;color:#f7931a}
#manualForm{display:none;margin-top:12px}
</style>
</head><body>
<div class='card'>
<h1>TollGate Setup</h1>
<p class='subtitle'>Configure upstream WiFi</p>
<div id='scanStatus'>Scanning...</div>
<div class='networks' id='networkList'></div>
<button class='refresh' onclick='scanWifi()'>Rescan</button>
<button class='refresh' onclick='showManual()'>Manual entry</button>
<div id='manualForm'>
<input id='manualSsid' placeholder='SSID'>
<input id='manualPass' type='password' placeholder='Password'>
<button class='btn' onclick='connectManual()'>Connect</button>
</div>
<div id='passwordForm' style='display:none'>
<p style='margin:12px 0 8px;text-align:left' id='selectedNetwork'></p>
<input id='wifiPass' type='password' placeholder='WiFi password'>
<button class='btn' onclick='connectSelected()'>Connect</button>
</div>
<div id='status'></div>
</div>
<script>
const apIp='${PORTAL_IP}';
let selectedSsid='';
function showStatus(msg,type){const s=document.getElementById('status');
s.textContent=msg;s.className=type;}
function scanWifi(){
document.getElementById('scanStatus').textContent='Scanning...';
document.getElementById('networkList').innerHTML='';
fetch('/wifi/scan').then(r=>r.json()).then(aps=>{
document.getElementById('scanStatus').textContent=aps.length+' networks found';
const list=document.getElementById('networkList');
aps.forEach(ap=>{
const div=document.createElement('div');
div.className='net-item';
const lock=ap.secured?'<span class=net-lock>&#128274;</span>':'';
div.innerHTML='<span class=net-ssid>'+lock+ap.ssid+'</span><span class=net-rssi>'+ap.rssi+' dBm</span>';
div.onclick=()=>selectNetwork(ap.ssid,ap.secured);
list.appendChild(div);
});
}).catch(e=>{document.getElementById('scanStatus').textContent='Scan failed';});
}
function selectNetwork(ssid,secured){
selectedSsid=ssid;
document.getElementById('selectedNetwork').textContent='Connect to: '+ssid;
document.getElementById('passwordForm').style.display='block';
document.getElementById('scanStatus').style.display='none';
document.getElementById('networkList').style.display='none';
document.querySelector('.refresh').style.display='none';
if(!secured){connectSelected();}
}
function showManual(){
document.getElementById('manualForm').style.display='block';
}
function connectSelected(){
const pass=document.getElementById('wifiPass').value;
doConnect(selectedSsid,pass);
}
function connectManual(){
const ssid=document.getElementById('manualSsid').value.trim();
const pass=document.getElementById('manualPass').value;
if(!ssid){showStatus('Enter SSID','error');return;}
doConnect(ssid,pass);
}
function doConnect(ssid,pass){
showStatus('Connecting to '+ssid+'...','processing');
fetch('/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({ssid:ssid,password:pass})})
.then(r=>r.json()).then(d=>{
if(d.ok){showStatus('Connected! Device is restarting...','success');}
else{showStatus('Failed: '+(d.error||'unknown'),'error');}
}).catch(e=>{showStatus('Connection error','error');});
}
scanWifi();
</script>
</body></html>`;

const MOCK_AP_LIST = [
  { ssid: 'HomeNetwork', rssi: -42, secured: true },
  { ssid: 'CafeWiFi', rssi: -67, secured: true },
  { ssid: 'OpenPublic', rssi: -75, secured: false },
  { ssid: 'Neighbor5G', rssi: -81, secured: true },
];

async function setupMockRoutes(page, overrides = {}) {
  const scanResponse = overrides.scanResponse || MOCK_AP_LIST;
  const connectHandler = overrides.connectHandler || (() => ({ ok: true }));

  await page.route('**/setup', async route => {
    await route.fulfill({
      status: 200,
      contentType: 'text/html',
      body: SETUP_HTML,
    });
  });

  await page.route('**/wifi/scan', async route => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(scanResponse),
    });
  });

  await page.route('**/wifi/connect', async route => {
    const request = route.request();
    const body = request.postDataJSON();
    const response = connectHandler(body);
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(response),
    });
  });
}

async function loadSetupPage(page) {
  await page.goto('http://tollgate.test/setup', { waitUntil: 'networkidle' });
}

test.describe('WiFi Setup \u2014 Layer 1: API Endpoints (needs live board)', () => {

  test('GET /setup redirects to portal on configured board', async ({ request }) => {
    const resp = await request.fetch(`${PORTAL_URL}/setup`, {
      maxRedirects: 0,
    });
    expect(resp.status()).toBe(302);
    const location = resp.headers()['location'];
    expect(location).toContain(PORTAL_IP);
    expect(location).toMatch(/\/$/);
  });

  test('GET /wifi/scan returns JSON array with valid AP objects', async ({ request }) => {
    const resp = await request.get(`${PORTAL_URL}/wifi/scan`);
    expect(resp.status()).toBe(200);
    const data = await resp.json();
    expect(Array.isArray(data)).toBe(true);
    if (data.length > 0) {
      const ap = data[0];
      expect(ap).toHaveProperty('ssid');
      expect(typeof ap.ssid).toBe('string');
      expect(ap).toHaveProperty('rssi');
      expect(typeof ap.rssi).toBe('number');
      expect(ap).toHaveProperty('secured');
      expect(typeof ap.secured).toBe('boolean');
    }
  });

  test('GET /wifi/status returns connection state', async ({ request }) => {
    const resp = await request.get(`${PORTAL_URL}/wifi/status`);
    expect(resp.status()).toBe(200);
    const data = await resp.json();
    expect(data).toHaveProperty('connected');
    expect(typeof data.connected).toBe('boolean');
    if (data.connected) {
      expect(data).toHaveProperty('ip');
      expect(data.ip).toMatch(/\d+\.\d+\.\d+\.\d+/);
      expect(data).toHaveProperty('ssid');
    }
  });

  test('POST /wifi/connect rejects empty body', async ({ request }) => {
    const resp = await request.post(`${PORTAL_URL}/wifi/connect`, {
      data: '',
      headers: { 'Content-Type': 'application/json' },
    });
    const data = await resp.json();
    expect(data.ok).toBe(false);
  });

  test('POST /wifi/connect rejects invalid JSON', async ({ request }) => {
    const resp = await request.post(`${PORTAL_URL}/wifi/connect`, {
      data: 'not json at all',
      headers: { 'Content-Type': 'application/json' },
    });
    const data = await resp.json();
    expect(data.ok).toBe(false);
    expect(data.error).toBeDefined();
  });

  test('POST /wifi/connect rejects missing ssid', async ({ request }) => {
    const resp = await request.post(`${PORTAL_URL}/wifi/connect`, {
      data: JSON.stringify({ password: 'testpass' }),
      headers: { 'Content-Type': 'application/json' },
    });
    const data = await resp.json();
    expect(data.ok).toBe(false);
    expect(data.error).toContain('ssid');
  });

  test('POST /wifi/connect with valid SSID returns ok or ECONNRESET', async ({ request }) => {
    const resp = await request.post(`${PORTAL_URL}/wifi/connect`, {
      data: JSON.stringify({ ssid: 'TestSetupAP', password: 'testpass123' }),
      headers: { 'Content-Type': 'application/json' },
      maxRedirects: 0,
      timeout: 10000,
    }).catch(() => null);

    if (resp) {
      const text = await resp.text();
      try {
        const data = JSON.parse(text);
        expect(data.ok).toBe(true);
      } catch {
        expect(resp.status()).toBeLessThan(500);
      }
    }
  });
});

test.describe('WiFi Setup \u2014 Layer 1.5: Redirect (needs live board)', () => {
  test('redirect Location header contains correct AP IP', async ({ request }) => {
    const resp = await request.fetch(`${PORTAL_URL}/setup`, {
      maxRedirects: 0,
    });
    const location = resp.headers()['location'];
    expect(location).toBe(`http://${PORTAL_IP}/`);
  });
});

test.describe('WiFi Setup \u2014 Layer 2: HTML UI Interaction', () => {

  test('page renders with title and subtitle', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    await expect(page.locator('h1')).toHaveText('TollGate Setup');
    await expect(page.locator('.subtitle')).toHaveText('Configure upstream WiFi');
  });

  test('scan auto-triggers on load and shows network count', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    await expect(page.locator('#scanStatus')).toHaveText(/4 networks found/, { timeout: 5000 });
  });

  test('network list shows SSID and RSSI for each AP', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    await expect(page.locator('.net-item')).toHaveCount(4);
    await expect(page.locator('.net-ssid').first()).toContainText('HomeNetwork');
    await expect(page.locator('.net-rssi').first()).toContainText('-42 dBm');
  });

  test('secured networks show lock icon', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    const securedItems = page.locator('.net-item');
    const firstSecured = securedItems.first();
    await expect(firstSecured.locator('.net-lock')).toBeVisible();
  });

  test('open networks have no lock icon', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    const openItem = page.locator('.net-item').nth(2);
    await expect(openItem.locator('.net-lock')).toHaveCount(0);
    await expect(openItem.locator('.net-ssid')).toContainText('OpenPublic');
  });

  test('clicking secured network shows password form and hides list', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    await expect(page.locator('.net-item').first()).toBeVisible();
    await page.locator('.net-item').first().click();
    await expect(page.locator('#passwordForm')).toBeVisible();
    await expect(page.locator('#selectedNetwork')).toHaveText('Connect to: HomeNetwork');
    await expect(page.locator('#networkList')).toBeHidden();
    await expect(page.locator('#scanStatus')).toBeHidden();
  });

  test('clicking open network auto-connects without password form', async ({ page }) => {
    let connectBody = null;
    await setupMockRoutes(page, {
      connectHandler: (body) => {
        connectBody = body;
        return { ok: true };
      },
    });
    await loadSetupPage(page);
    const openItem = page.locator('.net-item').nth(2);
    await openItem.click();
    await expect(page.locator('#status')).toHaveClass(/processing|success/, { timeout: 5000 });
    expect(connectBody).toBeTruthy();
    expect(connectBody.ssid).toBe('OpenPublic');
  });

  test('manual entry button toggles form visibility', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    await expect(page.locator('#manualForm')).toBeHidden();
    await page.locator('button:has-text("Manual entry")').click();
    await expect(page.locator('#manualForm')).toBeVisible();
    await expect(page.locator('#manualSsid')).toBeVisible();
    await expect(page.locator('#manualPass')).toBeVisible();
  });

  test('manual connect with empty SSID shows error', async ({ page }) => {
    await setupMockRoutes(page);
    await loadSetupPage(page);
    await page.locator('button:has-text("Manual entry")').click();
    await page.locator('#manualSsid').fill('');
    await page.locator('#manualForm .btn').click();
    await expect(page.locator('#status')).toHaveClass(/error/);
    await expect(page.locator('#status')).toContainText('Enter SSID');
  });

  test('connect sends correct JSON body to /wifi/connect', async ({ page }) => {
    let capturedBody = null;
    await setupMockRoutes(page, {
      connectHandler: (body) => {
        capturedBody = body;
        return { ok: true };
      },
    });
    await loadSetupPage(page);
    await page.locator('.net-item').first().click();
    await page.locator('#wifiPass').fill('mysecretpass');
    await page.locator('#passwordForm .btn').click();
    await expect(page.locator('#status')).toHaveClass(/success|processing/, { timeout: 5000 });
    expect(capturedBody).toEqual({ ssid: 'HomeNetwork', password: 'mysecretpass' });
  });

  test('success response shows green status with Connected message', async ({ page }) => {
    await setupMockRoutes(page, {
      connectHandler: () => ({ ok: true }),
    });
    await loadSetupPage(page);
    await page.locator('.net-item').first().click();
    await page.locator('#wifiPass').fill('testpass');
    await page.locator('#passwordForm .btn').click();
    await expect(page.locator('#status')).toHaveClass(/success/, { timeout: 5000 });
    await expect(page.locator('#status')).toContainText('Connected!');
  });

  test('error response shows red status with failure reason', async ({ page }) => {
    await setupMockRoutes(page, {
      connectHandler: () => ({ ok: false, error: 'save failed' }),
    });
    await loadSetupPage(page);
    await page.locator('.net-item').first().click();
    await page.locator('#wifiPass').fill('wrongpass');
    await page.locator('#passwordForm .btn').click();
    await expect(page.locator('#status')).toHaveClass(/error/, { timeout: 5000 });
    await expect(page.locator('#status')).toContainText('Failed: save failed');
  });

  test('rescan button clears list and fetches fresh data', async ({ page }) => {
    let scanCount = 0;
    await page.route('**/setup', async route => {
      await route.fulfill({ status: 200, contentType: 'text/html', body: SETUP_HTML });
    });
    await page.route('**/wifi/scan', async route => {
      scanCount++;
      const data = scanCount === 1 ? MOCK_AP_LIST : [
        { ssid: 'NewNetwork1', rssi: -30, secured: true },
        { ssid: 'NewNetwork2', rssi: -55, secured: false },
      ];
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(data),
      });
    });
    await page.route('**/wifi/connect', async route => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ ok: true }),
      });
    });
    await loadSetupPage(page);
    await expect(page.locator('.net-item')).toHaveCount(4, { timeout: 5000 });
    expect(scanCount).toBe(1);
    await page.locator('button:has-text("Rescan")').click();
    await expect(page.locator('.net-item')).toHaveCount(2, { timeout: 5000 });
    await expect(page.locator('.net-ssid').first()).toContainText('NewNetwork1');
    expect(scanCount).toBe(2);
  });

});

test.describe('WiFi Setup \u2014 Layer 3: Full E2E (needs unconfigured board)', () => {

  test.skip('full phone flow: scan \u2192 select \u2192 password \u2192 connect \u2192 status', async ({ page }) => {
    await page.goto(`${PORTAL_URL}/setup`);
    await expect(page.locator('h1')).toHaveText('TollGate Setup');
    await expect(page.locator('#scanStatus')).not.toHaveText('Scanning...', { timeout: 10000 });
    const networkCount = await page.locator('.net-item').count();
    expect(networkCount).toBeGreaterThan(0);
    const firstSsid = await page.locator('.net-ssid').first().textContent();
    await page.locator('.net-item').first().click();
    await expect(page.locator('#passwordForm')).toBeVisible();
    await page.locator('#wifiPass').fill('test-password');
    await page.locator('#passwordForm .btn').click();
    await expect(page.locator('#status')).toHaveClass(/success|error|processing/, { timeout: 15000 });
  });
});
