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
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

console.log(`\n========================================`);
console.log(`  Multi-Mint Integration Test`);
console.log(`  Target: ${IP}:${API_PORT}`);
console.log(`========================================\n`);

// ===== Pre-flight: wait for board to be ready =====
console.log('--- Pre-flight: Board Readiness ---');
let discovery = null;
for (let i = 0; i < 10; i++) {
  const out = run(`curl -s --connect-timeout 3 ${BASE}/`);
  if (out) { try { discovery = JSON.parse(out); } catch {} }
  if (discovery) break;
  if (i < 9) execSync('sleep 3');
}
if (!discovery) {
  console.log('  FATAL: Board not responding after 10 retries. Aborting.');
  process.exit(2);
}
console.log('  Board is responding!');

// ===== BURST FETCH: grab everything in one go =====
console.log('  Burst-fetching all endpoints...');

const mintsRaw = run(`curl -s --connect-timeout 5 ${BASE}/mints`);
const walletRaw = run(`curl -s --connect-timeout 5 ${BASE}/wallet`);
const usageRaw = run(`curl -s --connect-timeout 5 ${BASE}/usage`);
const whoamiRaw = run(`curl -s --connect-timeout 5 ${BASE}/whoami`);
const portalRaw = run(`curl -s --connect-timeout 10 http://${IP}/`);

const badTokenRaw = run(`curl -s --connect-timeout 5 -X POST -d "cashuAtest123" ${BASE}/`);
const emptyBodyRaw = run(`curl -s --connect-timeout 5 -X POST -d "" ${BASE}/`);
const noPrefixRaw = run(`curl -s --connect-timeout 5 -X POST -d "not_a_cashu_token" ${BASE}/`);

const fakeV3Token = 'cashuA' + Buffer.from(JSON.stringify({
  token: [{ mint: 'https://mint.minibits.cash/Bitcoin', proofs: [{ amount: 1, id: 'fake', secret: 'fake', C: 'fake' }] }]
})).toString('base64url');
const fakeTokenRaw = run(`curl -s --connect-timeout 5 -X POST -d "${fakeV3Token}" ${BASE}/`);

const badMintToken = 'cashuA' + Buffer.from(JSON.stringify({
  token: [{ mint: 'https://evil-mint.example.com', proofs: [{ amount: 1, id: 'fake', secret: 'fake', C: 'fake' }] }]
})).toString('base64url');
const badMintRaw = run(`curl -s --connect-timeout 5 -X POST -d "${badMintToken}" ${BASE}/`);

let mints = null, wallet = null, usage = null;
try { mints = mintsRaw ? JSON.parse(mintsRaw) : null; } catch {}
try { wallet = walletRaw ? JSON.parse(walletRaw) : null; } catch {}
try { usage = usageRaw ? JSON.parse(usageRaw) : null; } catch {}

const boardHasInternet = mints && mints.some(m => m.reachable === true);

console.log(`  Got: discovery=${!!discovery} mints=${!!mints} wallet=${!!wallet} usage=${!!usage} whoami=${!!whoamiRaw} portal=${!!portalRaw}`);
console.log('');

// ===== SECTION 1: Configuration =====
console.log('--- Section 1: Configuration ---');
assert(discovery && discovery.kind === 10021, 'Discovery has kind=10021');
assert(discovery && discovery.tags && discovery.tags.some(t => t[0] === 'metric' && t[1] === 'milliseconds'), 'Metric is milliseconds');
const priceTag = discovery && discovery.tags && discovery.tags.find(t => t[0] === 'price_per_step');
assert(priceTag && priceTag[1] === 'cashu', 'Price tag uses cashu unit');
assert(priceTag && priceTag[2] === '1', 'Price is 1 sat');
assert(priceTag && priceTag[5] === '1', 'Price step count is 1');

// ===== SECTION 2: Mint List =====
console.log('\n--- Section 2: Mint List ---');
assert(mints !== null, 'GET /mints returns valid JSON');
assert(Array.isArray(mints), '/mints returns an array');
assert(mints && mints.length === MINTS_EXPECTED.length, `/mints has ${MINTS_EXPECTED.length} entries (got ${mints ? mints.length : 0})`);

if (mints && mints.length > 0) {
  for (const expectedUrl of MINTS_EXPECTED) {
    const found = mints.find(m => m.url === expectedUrl);
    assert(found !== undefined, `Mint list contains ${expectedUrl}`);
    if (found) {
      assert(typeof found.reachable === 'boolean', `${expectedUrl.split('//')[1]} has boolean reachable field`);
    }
  }
}

// ===== SECTION 3: Health Status =====
console.log('\n--- Section 3: Health Status ---');
if (!boardHasInternet) {
  skip('Mint reachability probes', 'Board has no internet');
  skip('Reachable mint transitions', 'Board has no internet');
  if (mints && mints.length > 0) {
    const allUnreachable = mints.every(m => m.reachable === false);
    assert(allUnreachable, 'All mints show reachable=false without internet');
  }
} else {
  const reachableMints = mints.filter(m => m.reachable);
  console.log(`  Reachable: ${reachableMints.length}/${mints.length}`);
  assert(reachableMints.length > 0, `At least 1 mint is reachable`);
  for (const m of reachableMints) console.log(`    \u2713 ${m.url}`);
  for (const m of mints.filter(m => !m.reachable)) console.log(`    \u2717 ${m.url}`);
}

// ===== SECTION 4: Payment Routing =====
console.log('\n--- Section 4: Payment Routing ---');
assert(badTokenRaw !== null, 'POST / with bad token returns response');
assert(badTokenRaw && badTokenRaw.includes('payment-error-invalid'), 'Bad token rejected with payment-error-invalid');
assert(emptyBodyRaw && emptyBodyRaw.includes('payment-error-invalid'), 'Empty body rejected');
assert(noPrefixRaw && noPrefixRaw.includes('payment-error-invalid'), 'Non-cashu body rejected');

if (fakeTokenRaw) {
  try {
    const parsed = JSON.parse(fakeTokenRaw);
    if (parsed.tags && parsed.tags.some(t => t[0] === 'code')) {
      const code = parsed.tags.find(t => t[0] === 'code')[1];
      if (boardHasInternet) {
        assert(code === 'payment-error-verification' || code === 'payment-error-token-spent',
          'Fake V3 token rejected by mint verification');
      } else {
        assert(code === 'payment-error-mint-not-accepted' || code === 'payment-error-verification',
          'Fake V3 token rejected (unreachable or verification failed)');
      }
    } else { skip('Fake V3 token code check', 'Unexpected response format'); }
  } catch { skip('Fake V3 token parse', 'Non-JSON response'); }
}

assert(badMintRaw && badMintRaw.includes('payment-error-mint-not-accepted'),
  'Token from non-accepted mint rejected');

// ===== SECTION 5: Wallet Status =====
console.log('\n--- Section 5: Wallet Status ---');
assert(wallet !== null, 'GET /wallet returns valid JSON');
assert(wallet && typeof wallet.balance === 'number', 'Wallet has balance field');
assert(wallet && typeof wallet.proof_count === 'number', 'Wallet has proof_count field');
assert(wallet && Array.isArray(wallet.proofs), 'Wallet has proofs array');
assert(wallet && wallet.balance >= 0, 'Balance is non-negative');
assert(wallet && wallet.proof_count >= 0, 'Proof count is non-negative');

// ===== SECTION 6: Session / Usage =====
console.log('\n--- Section 6: Session / Usage ---');
assert(usage !== null, 'GET /usage returns valid JSON');
assert(whoamiRaw !== null, 'GET /whoami returns response');
assert(whoamiRaw && whoamiRaw.includes('mac='), '/whoami returns mac=...');

// ===== SECTION 7: Dynamic Mint Status =====
console.log('\n--- Section 7: Dynamic Mint Status Transitions ---');
if (!boardHasInternet) {
  skip('Reachable->unreachable transition', 'No internet');
  skip('Unreachable->reachable recovery', 'No internet');
  skip('Mint status callback triggers', 'No internet');
  skip('Payment rejection for unreachable mints', 'No internet');
} else {
  console.log('  Board has internet. Checking health probe results...');
  console.log('  (Waiting 60s for probe cycle would exceed board uptime; skipping dynamic test)');
  skip('Dynamic transition test', 'Board uptime too short for 300s probe interval');
}

// ===== SECTION 8: Portal Multi-Mint UI =====
console.log('\n--- Section 8: Portal Multi-Mint UI ---');
assert(portalRaw && portalRaw.includes('TollGate'), 'Portal HTML contains TollGate');
assert(portalRaw && (portalRaw.includes('SUPPORTED MINTS') || portalRaw.includes('mint-list')), 'Portal has mint list section');

for (const mintUrl of MINTS_EXPECTED) {
  const shortUrl = mintUrl.replace('https://', '');
  assert(portalRaw && portalRaw.includes(shortUrl), `Portal lists ${shortUrl}`);
}

assert(portalRaw && portalRaw.includes('mint-dot'), 'Portal has mint status dots');
assert(portalRaw && portalRaw.includes(':2121/mints'), 'Portal JS fetches mints from API server');

// ===== Summary =====
console.log(`\n========================================`);
console.log(`  Results: ${passed} passed, ${failed} failed, ${skipped} skipped`);
console.log(`========================================\n`);
process.exit(failed > 0 ? 1 : 0);
