import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.185.47.1';

console.log(`\n=== WiFi Setup Integration Test ===`);
console.log(`Portal IP: ${IP}\n`);

let passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { console.log(`  PASS: ${msg}`); passed++; }
  else { console.log(`  FAIL: ${msg}`); failed++; }
}

function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 15000 }); }
  catch { return null; }
}

function fetchJSON(path) {
  const result = run(`curl -s --connect-timeout 5 http://${IP}${path}`);
  if (!result) return null;
  try { return JSON.parse(result); }
  catch { return null; }
}

// 1. /setup page returns HTML (or redirects if already configured)
const setupPage = run(`curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 http://${IP}/setup`);
assert(setupPage === '200' || setupPage === '302', `/setup returns 200 or 302 (got ${setupPage})`);

// 2. /wifi/status endpoint works
const status = fetchJSON('/wifi/status');
assert(status !== null, '/wifi/status returns JSON');
assert(typeof status.connected === 'boolean', '/wifi/status has connected field');

// 3. /wifi/scan endpoint returns array
console.log('\n  (wifi/scan may take a few seconds...)');
const scanResult = run(`curl -s --connect-timeout 15 --max-time 15 http://${IP}/wifi/scan`);
let scanData = null;
if (scanResult) {
  try { scanData = JSON.parse(scanResult); } catch {}
}
assert(scanData !== null, '/wifi/scan returns JSON');
if (scanData && Array.isArray(scanData)) {
  assert(scanData.length >= 0, `/wifi/scan returns array (${scanData.length} APs)`);
  if (scanData.length > 0) {
    const ap = scanData[0];
    assert(ap.ssid !== undefined, 'AP has ssid field');
    assert(ap.rssi !== undefined, 'AP has rssi field');
    assert(ap.secured !== undefined, 'AP has secured field');
    console.log(`    First AP: "${ap.ssid}" (${ap.rssi} dBm, ${ap.secured ? 'secured' : 'open'})`);
  }
}

// 4. /wifi/connect rejects invalid JSON
const badConnect = run(`curl -s -X POST -d 'not json' --connect-timeout 5 http://${IP}/wifi/connect`);
assert(badConnect !== null, '/wifi/connect responds to bad request');
if (badConnect) {
  try {
    const err = JSON.parse(badConnect);
    assert(err.ok === false, '/wifi/connect returns ok:false for bad request');
  } catch {}
}

// 5. /wifi/connect rejects missing ssid
const noSsid = run(`curl -s -X POST -H 'Content-Type: application/json' -d '{}' --connect-timeout 5 http://${IP}/wifi/connect`);
if (noSsid) {
  try {
    const err = JSON.parse(noSsid);
    assert(err.ok === false && err.error, '/wifi/connect returns error for missing ssid');
  } catch {}
}

console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
process.exit(failed > 0 ? 1 : 0);
