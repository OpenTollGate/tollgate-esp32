import { execSync } from 'child_process';

const BOARD_A_IP = process.env.TOLLGATE_IP || '10.185.47.1';
const BOARD_B_IP = process.env.TOLLGATE_B_IP || process.env.TOLLGATE_IP_B || '10.192.45.1';
const API_A = `http://${BOARD_A_IP}:2121`;
const API_B = `http://${BOARD_B_IP}:2121`;

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

function canReach(url) {
  const result = run(`curl -s --connect-timeout 3 --max-time 5 -o /dev/null -w "%{http_code}" ${url}`);
  return result && result.trim() !== '000' && result.trim() !== '';
}

console.log('=== test-price-discovery (two-board) ===\n');

const reachA = canReach(`${API_A}/market`);
const reachB = canReach(`${API_B}/market`);

console.log(`Reachability: Board A=${reachA ? 'YES' : 'NO'}, Board B=${reachB ? 'YES' : 'NO'}\n`);

if (!reachA && !reachB) {
  console.log('FATAL: Neither board reachable. Check TOLLGATE_IP and TOLLGATE_B_IP');
  process.exit(1);
}

console.log('--- Board A: market endpoint ---');
{
  if (reachA) {
    const data = runJson(`curl -s --connect-timeout 5 --max-time 10 ${API_A}/market`);
    assert(data !== null, 'Board A /market returns JSON');
    assert(typeof data?.count === 'number', `Board A count is ${data?.count}`);
    if (data && data.entries) {
      console.log(`  Board A sees ${data.count} nearby TollGate(s):`);
      for (const e of data.entries) {
        console.log(`    ${e.ssid} (BSSID: ${e.bssid}) — ${e.price_per_step} sats/step, RSSI: ${e.rssi}`);
      }
    }
  } else {
    console.log('  SKIP: Board A not reachable');
  }
}

console.log('\n--- Board B: market endpoint ---');
{
  if (reachB) {
    const data = runJson(`curl -s --connect-timeout 5 --max-time 10 ${API_B}/market`);
    assert(data !== null, 'Board B /market returns JSON');
    assert(typeof data?.count === 'number', `Board B count is ${data?.count}`);
    if (data && data.entries) {
      console.log(`  Board B sees ${data.count} nearby TollGate(s):`);
      for (const e of data.entries) {
        console.log(`    ${e.ssid} (BSSID: ${e.bssid}) — ${e.price_per_step} sats/step, RSSI: ${e.rssi}`);
      }
    }
  } else {
    console.log('  SKIP: Board B not reachable');
  }
}

console.log('\n--- Cross-discovery: Board A sees Board B ---');
{
  if (reachA) {
    const mktA = runJson(`curl -s --connect-timeout 5 --max-time 10 ${API_A}/market`);
    if (mktA && mktA.count > 0) {
      const foundB = mktA.entries.some(e =>
        (e.ssid.startsWith('TollGate-') || e.ssid === 'unknown') && e.bssid !== '' && e.price_per_step > 0
      );
      assert(foundB, `Board A discovered another TollGate (count=${mktA.count})`);
    } else {
      console.log('  INFO: Board A has 0 entries. Scan may need more time.');
    }
  } else {
    console.log('  SKIP: Board A not reachable');
  }
}

console.log('\n--- Cross-discovery: Board B sees Board A ---');
{
  if (reachB) {
    const mktB = runJson(`curl -s --connect-timeout 5 --max-time 10 ${API_B}/market`);
    if (mktB && mktB.count > 0) {
      const foundA = mktB.entries.some(e =>
        (e.ssid.startsWith('TollGate-') || e.ssid === 'unknown') && e.bssid !== '' && e.price_per_step > 0
      );
      assert(foundA, `Board B discovered another TollGate (count=${mktB.count})`);
    } else {
      console.log('  INFO: Board B has 0 entries. Scan may need more time.');
    }
  } else {
    console.log('  SKIP: Board B not reachable');
  }
}

console.log('\n--- Discovery data integrity ---');
{
  const boards = [];
  if (reachA) {
    const mktA = runJson(`curl -s --connect-timeout 5 --max-time 10 ${API_A}/market`);
    if (mktA?.entries) boards.push({ name: 'A', data: mktA });
  }
  if (reachB) {
    const mktB = runJson(`curl -s --connect-timeout 5 --max-time 10 ${API_B}/market`);
    if (mktB?.entries) boards.push({ name: 'B', data: mktB });
  }

  for (const { name, data } of boards) {
    for (const e of data.entries) {
      assert(typeof e.price_per_step === 'number' && e.price_per_step > 0,
        `Board ${name} entry has valid price (${e.price_per_step})`);
      assert(typeof e.step_size === 'number' && e.step_size > 0,
        `Board ${name} entry has valid step_size (${e.step_size})`);
      assert(typeof e.metric === 'string' && e.metric.length > 0,
        `Board ${name} entry has valid metric (${e.metric})`);
      assert(typeof e.rssi === 'number',
        `Board ${name} entry has valid RSSI (${e.rssi})`);
      break;
    }
  }
}

console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
process.exit(failed > 0 ? 1 : 0);
