import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.192.45.1';
const API_PORT = 2121;
const BASE = `http://${IP}:${API_PORT}`;
const MINTS_EXPECTED = [
  'https://mint.minibits.cash/Bitcoin',
  'https://mint.coinos.io',
  'https://21mint.me',
  'https://mint.lnvoltz.com',
];
let passed = 0, failed = 0, skipped = 0;

function assert(condition, test) {
  if (condition) { console.log(`  \u2713 ${test}`); passed++; }
  else { console.log(`  \u2717 ${test}`); failed++; }
}
function skip(test, reason) {
  console.log(`  \u25CB ${test} (SKIPPED: ${reason})`); skipped++;
}
function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 30000 }); }
  catch (e) { return e.stdout || null; }
}
function json(url) {
  const out = run(`curl -s --connect-timeout 5 ${url}`);
  if (!out) return null;
  try { return JSON.parse(out); }
  catch { return null; }
}
function jsonRetry(url, retries = 5, delayMs = 2000) {
  for (let i = 0; i < retries; i++) {
    const result = json(url);
    if (result !== null) return result;
    if (i < retries - 1) {
      console.log(`    (retry ${i+1}/${retries}: ${url})`);
      execSync(`sleep ${delayMs/1000}`);
    }
  }
  return null;
}
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

console.log(`\n========================================`);
console.log(`  Multi-Mint Integration Test`);
console.log(`  Target: ${IP}:${API_PORT}`);
console.log(`========================================\n`);

// ===== Pre-flight: wait for board to be ready =====
console.log('--- Pre-flight: Board Readiness ---');
const discovery = jsonRetry(`${BASE}/`, 8, 3000);
if (!discovery) {
  console.log('  FATAL: Board not responding after 8 retries. Aborting.');
  process.exit(2);
}
console.log('  Board is responding!\n');

// ===== SECTION 1: Configuration =====
console.log('--- Section 1: Configuration ---');

assert(discovery !== null, 'GET / returns valid JSON');
assert(discovery && discovery.kind === 10021, 'Discovery has kind=10021');
assert(discovery && discovery.tags && discovery.tags.some(t => t[0] === 'metric' && t[1] === 'milliseconds'), 'Metric is milliseconds');

const priceTag = discovery && discovery.tags && discovery.tags.find(t => t[0] === 'price_per_step');
assert(priceTag && priceTag[1] === 'cashu', 'Price tag uses cashu unit');
assert(priceTag && priceTag[2] === '1', 'Price is 1 sat');
assert(priceTag && priceTag[5] === '1', 'Price step count is 1');

// ===== SECTION 2: Mint List =====
console.log('\n--- Section 2: Mint List ---');

// Batch fetch mints immediately after discovery (board is unstable)
const mintsRaw = run(`curl -s --connect-timeout 5 ${BASE}/mints`);
let mints = null;
try { mints = mintsRaw ? JSON.parse(mintsRaw) : null; } catch { mints = null; }
assert(mints !== null, 'GET /mints returns valid JSON');
assert(Array.isArray(mints), '/mints returns an array');
assert(mints && mints.length === MINTS_EXPECTED.length, `/mints has ${MINTS_EXPECTED.length} entries (got ${mints ? mints.length : 0})`);

if (mints && mints.length > 0) {
  for (const expectedUrl of MINTS_EXPECTED) {
    const found = mints.find(m => m.url === expectedUrl);
    assert(found !== undefined, `Mint list contains ${expectedUrl}`);
    if (found) {
      assert(typeof found.reachable === 'boolean', `${expectedUrl} has boolean reachable field`);
    }
  }
}

// ===== SECTION 3: Health Status =====
console.log('\n--- Section 3: Health Status ---');

const hasHostInternet = run('ping -c 1 -W 3 8.8.8.8 2>/dev/null');
const boardHasInternet = (() => {
  if (!discovery) return false;
  // If board has STA internet, mints would be reachable after initial probe
  // Check by seeing if any mint is reachable
  const m = jsonRetry(`${BASE}/mints`, 3, 1000);
  return m && m.some(mi => mi.reachable === true);
})();

if (!boardHasInternet) {
  skip('Mint reachability probes', 'Board has no internet connectivity');
  skip('Reachable mint transitions', 'Board has no internet connectivity');
  
  if (mints && mints.length > 0) {
    const allUnreachable = mints.every(m => m.reachable === false);
    assert(allUnreachable, 'All mints show reachable=false without internet');
  }
} else {
  console.log('  Board has internet! Running live health probe tests...');
  
  const reachableMints = mints ? mints.filter(m => m.reachable) : [];
  const unreachableMints = mints ? mints.filter(m => !m.reachable) : [];
  
  console.log(`  Reachable: ${reachableMints.length}, Unreachable: ${unreachableMints.length}`);
  assert(reachableMints.length > 0, `At least 1 mint is reachable (got ${reachableMints.length})`);
  
  for (const m of reachableMints) {
    console.log(`  \u2713 REACHABLE: ${m.url}`);
  }
  for (const m of unreachableMints) {
    console.log(`  \u2717 UNREACHABLE: ${m.url}`);
  }
}

// ===== SECTION 4: Payment Routing =====
console.log('\n--- Section 4: Payment Routing ---');

const badTokenResp = run(`curl -s --connect-timeout 5 -X POST -d "cashuAtest123" ${BASE}/`);
assert(badTokenResp !== null, 'POST / with bad token returns response');
assert(badTokenResp && badTokenResp.includes('payment-error-invalid'), 'Bad token rejected with payment-error-invalid');

const emptyBodyResp = run(`curl -s --connect-timeout 5 -X POST -d "" ${BASE}/`);
assert(emptyBodyResp && emptyBodyResp.includes('payment-error-invalid'), 'Empty body rejected');

const noPrefixResp = run(`curl -s --connect-timeout 5 -X POST -d "not_a_cashu_token" ${BASE}/`);
assert(noPrefixResp && noPrefixResp.includes('payment-error-invalid'), 'Non-cashu body rejected');

// Test with a V3 token structure but fake proofs
const fakeV3Token = 'cashuA' + Buffer.from(JSON.stringify({
  token: [{ mint: 'https://mint.minibits.cash/Bitcoin', proofs: [{ amount: 1, id: 'fake', secret: 'fake', C: 'fake' }] }]
})).toString('base64url');

const fakeTokenResp = run(`curl -s --connect-timeout 5 -X POST -d "${fakeV3Token}" ${BASE}/`);
if (fakeTokenResp) {
  try {
    const parsed = JSON.parse(fakeTokenResp);
    if (parsed.tags && parsed.tags.some(t => t[0] === 'code')) {
      const code = parsed.tags.find(t => t[0] === 'code')[1];
      if (boardHasInternet) {
        assert(code === 'payment-error-verification' || code === 'payment-error-token-spent',
          'Fake V3 token rejected by mint verification (not locally)');
      } else {
        assert(code === 'payment-error-mint-not-accepted' || code === 'payment-error-verification',
          'Fake V3 token rejected (mint unreachable or verification failed)');
      }
    } else {
      skip('Fake V3 token code check', 'Response has unexpected format');
    }
  } catch {
    skip('Fake V3 token parse', 'Non-JSON response');
  }
}

// Test with token from non-accepted mint
const badMintToken = 'cashuA' + Buffer.from(JSON.stringify({
  token: [{ mint: 'https://evil-mint.example.com', proofs: [{ amount: 1, id: 'fake', secret: 'fake', C: 'fake' }] }]
})).toString('base64url');

const badMintResp = run(`curl -s --connect-timeout 5 -X POST -d "${badMintToken}" ${BASE}/`);
assert(badMintResp && badMintResp.includes('payment-error-mint-not-accepted'),
  'Token from non-accepted mint rejected');

// ===== SECTION 5: Wallet Status =====
console.log('\n--- Section 5: Wallet Status ---');

const wallet = jsonRetry(`${BASE}/wallet`, 3, 1000);
assert(wallet !== null, 'GET /wallet returns valid JSON');
assert(wallet && typeof wallet.balance === 'number', 'Wallet has balance field');
assert(wallet && typeof wallet.proof_count === 'number', 'Wallet has proof_count field');
assert(wallet && Array.isArray(wallet.proofs), 'Wallet has proofs array');
assert(wallet && wallet.balance >= 0, 'Balance is non-negative');
assert(wallet && wallet.proof_count >= 0, 'Proof count is non-negative');

// ===== SECTION 6: Session / Usage =====
console.log('\n--- Section 6: Session / Usage ---');

const usage = json(`${BASE}/usage`);
assert(usage !== null, 'GET /usage returns valid JSON');

const whoami = run(`curl -s --connect-timeout 5 ${BASE}/whoami`);
assert(whoami !== null, 'GET /whoami returns response');
assert(whoami && whoami.includes('mac='), '/whoami returns mac=...');

// ===== SECTION 7: Dynamic Mint Status =====
console.log('\n--- Section 7: Dynamic Mint Status Transitions ---');

if (!boardHasInternet) {
  skip('Reachable->unreachable transition', 'No internet');
  skip('Unreachable->reachable recovery', 'No internet');
  skip('Mint status callback triggers', 'No internet');
  skip('Payment rejection for unreachable mints', 'No internet');
} else {
  // Wait for health probes to run and check if any mints became reachable
  console.log('  Waiting 60s for health probes to complete...');
  await sleep(60000);
  
  const mintsAfterProbe = json(`${BASE}/mints`);
  if (mintsAfterProbe) {
    const reachableNow = mintsAfterProbe.filter(m => m.reachable);
    console.log(`  After 60s: ${reachableNow.length}/${mintsAfterProbe.length} mints reachable`);
    
    // Compare with initial state
    const initialReachable = mints ? mints.filter(m => m.reachable).length : 0;
    if (reachableNow.length !== initialReachable) {
      console.log(`  \u271f Mint status changed: ${initialReachable} -> ${reachableNow.length} reachable`);
    }
    
    // Test payment only with a reachable mint
    if (reachableNow.length > 0) {
      console.log(`  \u2713 Can attempt payment with reachable mint: ${reachableNow[0].url}`);
    }
  }
}

// ===== SECTION 8: Portal Multi-Mint UI =====
console.log('\n--- Section 8: Portal Multi-Mint UI ---');

const portal = run(`curl -s --connect-timeout 5 http://${IP}/`);
assert(portal && portal.includes('TollGate'), 'Portal HTML contains TollGate');
assert(portal && portal.includes('SUPPORTED MINTS') || portal && portal.includes('mint-list'), 'Portal has mint list section');

for (const mintUrl of MINTS_EXPECTED) {
  const shortUrl = mintUrl.replace('https://', '');
  assert(portal && portal.includes(shortUrl), `Portal lists ${shortUrl}`);
}

assert(portal && portal.includes('mint-dot'), 'Portal has mint status dots');
assert(portal && portal.includes(':2121/mints'), 'Portal JS fetches mints from API server');

// ===== Summary =====
console.log(`\n========================================`);
console.log(`  Results: ${passed} passed, ${failed} failed, ${skipped} skipped`);
console.log(`========================================\n`);
process.exit(failed > 0 ? 1 : 0);
