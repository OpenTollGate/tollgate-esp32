import { execSync } from 'child_process';

const API_URL = `http://${process.env.TOLLGATE_IP || '10.185.47.1'}:2121`;

function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 15000 }); }
  catch (e) { return e.stdout || null; }
}

function runJson(cmd) {
  const out = run(cmd);
  try { return out ? JSON.parse(out) : null; }
  catch { return null; }
}

let passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { console.log(`  PASS: ${msg}`); passed++; }
  else { console.log(`  FAIL: ${msg}`); failed++; }
}

console.log('=== test-market (GET /market) ===\n');

console.log('--- /market endpoint responds ---');
{
  const data = runJson(`curl -s --connect-timeout 5 ${API_URL}/market`);
  assert(data !== null, '/market returns valid JSON');
  assert(typeof data.count === 'number', `count is number (got ${data?.count})`);
  assert(Array.isArray(data.entries), 'entries is array');
}

console.log('\n--- /market entry structure ---');
{
  const data = runJson(`curl -s --connect-timeout 5 ${API_URL}/market`);
  if (data && data.entries && data.entries.length > 0) {
    const e = data.entries[0];
    assert(typeof e.bssid === 'string', `bssid is string (got ${e.bssid})`);
    assert(typeof e.ssid === 'string', `ssid is string (got ${e.ssid})`);
    assert(typeof e.rssi === 'number', `rssi is number (got ${e.rssi})`);
    assert(typeof e.price_per_step === 'number', `price_per_step is number (got ${e.price_per_step})`);
    assert(typeof e.step_size === 'number', `step_size is number (got ${e.step_size})`);
    assert(typeof e.metric === 'string', `metric is string (got ${e.metric})`);
  } else {
    console.log('  SKIP: no entries found (scan may not have run yet)');
  }
}

console.log('\n--- /market with no discovered TollGates ---');
{
  const data = runJson(`curl -s --connect-timeout 5 ${API_URL}/market`);
  if (data && data.count === 0) {
    assert(data.entries.length === 0, 'empty entries array when count=0');
    console.log('  INFO: no nearby TollGates discovered yet (expected if only one board)');
  } else if (data && data.count > 0) {
    console.log(`  INFO: ${data.count} nearby TollGate(s) discovered`);
  }
}

console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
process.exit(failed > 0 ? 1 : 0);
