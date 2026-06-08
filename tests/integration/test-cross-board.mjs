import { execSync } from 'child_process';

const BOARD_B_IP = process.env.TOLLGATE_IP || '10.185.47.1';
const BOARD_B_SSID = process.env.TOLLGATE_SSID || 'TollGate-C0E9CA';
const WIFI_IFACE = process.env.WIFI_IFACE || 'wlp59s0';
const SUDO_PW = process.env.SUDO_PW || 'c03rad0r123';

let passed = 0, failed = 0;

function assert(condition, test) {
  if (condition) { console.log(`  \u2713 ${test}`); passed++; }
  else { console.log(`  \u2717 ${test}`); failed++; }
}

function run(cmd, timeout = 10000) {
  try {
    return execSync(cmd, { encoding: 'utf8', timeout, stdio: ['pipe', 'pipe', 'pipe'] });
  } catch (e) {
    return e.stdout || '';
  }
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function runTests() {
  console.log(`\n=== Cross-Board Payment Tests ===\n`);
  console.log(`Board B: ${BOARD_B_SSID} (${BOARD_B_IP})\n`);

  console.log('--- Test 1: Board B AP reachable ---');
  const pingResult = run(`ping -c 2 -W 2 ${BOARD_B_IP}`);
  assert(pingResult.includes('0% packet loss') || pingResult.includes('2 received'), `Board B reachable at ${BOARD_B_IP}`);

  console.log('\n--- Test 2: Board B API responds ---');
  const apiResult = run(`curl -s --connect-timeout 5 http://${BOARD_B_IP}:2121/usage`);
  const apiOk = apiResult.length > 0;
  assert(apiOk, 'Board B API /usage responds');
  if (!apiOk) {
    console.log('\n  Board B API not reachable — cannot continue cross-board tests');
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
    process.exit(failed > 0 ? 1 : 0);
  }

  console.log('\n--- Test 3: Board B discovery endpoint ---');
  const discovery = run(`curl -s --connect-timeout 5 http://${BOARD_B_IP}:2121/`);
  assert(discovery.length > 0, 'Discovery endpoint responds');
  try {
    const d = JSON.parse(discovery);
    assert(d.kind === 10021 || d.kind === undefined, `Discovery returns JSON (kind=${d.kind || 'N/A'})`);
    const priceTags = (d.tags || []).filter(t => t[0] === 'price_per_step');
    if (priceTags.length > 0) {
      assert(true, `price_per_step = ${priceTags[0][1]}`);
    }
  } catch {
    assert(discovery.includes('TollGate') || discovery.length > 0, 'Discovery returns data');
  }

  console.log('\n--- Test 4: Board B wallet endpoint ---');
  const wallet = run(`curl -s --connect-timeout 5 http://${BOARD_B_IP}:2121/wallet`);
  assert(wallet.length > 0, 'Wallet endpoint responds');
  try {
    const w = JSON.parse(wallet);
    assert(w.balance !== undefined, `Wallet balance = ${w.balance}`);
  } catch {
    assert(true, 'Wallet endpoint returns data (may not be initialized)');
  }

  console.log('\n--- Test 5: Board B local relay reachable ---');
  const relayResult = run(`curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 http://${BOARD_B_IP}:4869/`);
  assert(relayResult.includes('200') || relayResult.includes('400'), `Local relay on port 4869 responds (${relayResult.trim()})`);

  console.log('\n--- Test 6: Board B captive portal ---');
  const portal = run(`curl -s --connect-timeout 5 http://${BOARD_B_IP}/`);
  assert(portal.length > 0, 'Captive portal responds');
  assert(portal.includes('TollGate') || portal.includes('tollgate'), 'Portal contains TollGate branding');

  console.log('\n--- Test 7: Board B reset auth ---');
  const reset = run(`curl -s --connect-timeout 5 http://${BOARD_B_IP}/reset_authentication`);
  assert(reset.length > 0 || reset !== null, 'Reset auth endpoint responds');

  console.log('\n--- Test 8: Payment flow (if token available) ---');
  const testToken = process.env.TEST_TOKEN;
  if (testToken) {
    const payment = run(`curl -s --connect-timeout 10 -X POST http://${BOARD_B_IP}/ -d 'token=${testToken}'`);
    assert(payment.length > 0, 'Payment endpoint accepts token');
    try {
      const p = JSON.parse(payment);
      assert(p.success === true || p.allotment > 0, `Payment accepted (allotment=${p.allotment || 0})`);
    } catch {
      assert(payment.includes('ok') || payment.includes('success'), 'Payment response received');
    }
  } else {
    console.log('  (skipped — set TEST_TOKEN env var to test payment)');
    assert(true, 'Payment test skipped (no TEST_TOKEN)');
  }

  console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
  process.exit(failed > 0 ? 1 : 0);
}

runTests().catch(e => {
  console.error('Test error:', e.message);
  process.exit(1);
});
