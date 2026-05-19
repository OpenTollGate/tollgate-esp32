import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.192.45.1';
const API = `http://${IP}:2121`;
let passed = 0, failed = 0;

function assert(condition, test) {
  if (condition) { console.log(`  ✓ ${test}`); passed++; }
  else { console.log(`  ✗ ${test}`); failed++; }
}

function curlBody(url, options = {}) {
  const cmd = options.method
    ? `curl -s --connect-timeout 5 --max-time 10 -X ${options.method} ${options.data ? `-d '${options.data.replace(/'/g, "'\\''")}'` : ''} "${url}"`
    : `curl -s --connect-timeout 5 --max-time 10 "${url}"`;
  try { return execSync(cmd, { encoding: 'utf8', timeout: 90000 }); }
  catch { return null; }
}

function curlStatus(url, options = {}) {
  const cmd = `curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time 10 ${options.method ? `-X ${options.method}` : ''} ${options.data ? `-d '${options.data.replace(/'/g, "'\\''")}'` : ''} "${url}"`;
  try { return execSync(cmd, { encoding: 'utf8', timeout: 90000 }).trim(); }
  catch { return null; }
}

async function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

console.log(`\n=== Phase 2 Tests (target: ${API}) ===\n`);

// Test 15: Advertisement valid
console.log('Test 15: GET :2121/ returns kind=10021 advertisement');
const body15 = curlBody(`${API}/`);
const json15 = body15 ? JSON.parse(body15) : null;
assert(json15 && json15.kind === 10021, 'kind=10021');
assert(json15 && json15.tags && json15.tags.some(t => t[0] === 'price_per_step'), 'Has price_per_step tag');
assert(json15 && json15.tags && json15.tags.some(t => t[0] === 'step_size'), 'Has step_size tag');
assert(json15 && json15.tags && json15.tags.some(t => t[0] === 'metric'), 'Has metric tag');

// Test 19: Invalid token
console.log('\nTest 19: POST :2121/ with invalid token');
const body19 = curlBody(`${API}/`, { method: 'POST', data: 'garbage_not_a_token' });
const json19 = body19 ? JSON.parse(body19) : null;
assert(json19 && json19.kind === 21023, 'Returns kind=21023 notice');
assert(json19 && json19.tags && json19.tags.some(t => t[0] === 'code'), 'Has error code tag');
const status19 = curlStatus(`${API}/`, { method: 'POST', data: 'garbage_not_a_token' });
assert(status19 === '402' || status19 === '400', `Returns HTTP error (${status19})`);

// Test 21: Wrong mint (token from wrong mint)
console.log('\nTest 21: POST :2121/ with wrong mint token');
const wrongMintToken = 'cashuA' + Buffer.from(JSON.stringify({
  token: [{ mint: 'https://wrong.mint.example.com', proofs: [{ amount: 21, secret: 'test', id: '00'.repeat(8), C: '02'.repeat(33) }] }]
})).toString('base64url');
const body21 = curlBody(`${API}/`, { method: 'POST', data: wrongMintToken });
const json21 = body21 ? JSON.parse(body21) : null;
assert(json21 && json21.kind === 21023, 'Returns kind=21023');
const codeTag21 = json21 && json21.tags && json21.tags.find(t => t[0] === 'code');
assert(codeTag21 && (codeTag21[1] === 'payment-error-mint-not-accepted' || codeTag21[1] === 'payment-error'), `Error code: ${codeTag21 ? codeTag21[1] : 'none'}`);

// Test valid token (if provided)
const TEST_TOKEN = process.env.TEST_TOKEN;
if (TEST_TOKEN) {
  console.log('\nTest 16: POST :2121/ with valid token');
  const body16 = curlBody(`${API}/`, { method: 'POST', data: TEST_TOKEN });
  const json16 = body16 ? JSON.parse(body16) : null;
  assert(json16 && json16.kind === 1022, 'Returns kind=1022 session');
  assert(json16 && json16.tags && json16.tags.some(t => t[0] === 'allotment'), 'Has allotment tag');

  // Test 17: Usage tracking
  console.log('\nTest 17: GET :2121/usage after payment');
  const body17 = curlBody(`${API}/usage`);
  assert(body17 && !body17.includes('-1/-1'), 'Returns active usage');

  // Test 18: Internet after payment
  console.log('\nTest 18: Internet works after payment');
  await sleep(1500);
  const sudoPw = process.env.SUDO_PW || 'c03rad0r123';
  try {
    execSync(`echo '${sudoPw}' | sudo -S ip route add default via ${IP} dev wlp59s0 metric 50 2>/dev/null`, { encoding: 'utf8', timeout: 5000 });
  } catch {}
  let pingOk = false;
  try {
    const ping18 = execSync('ping -c 3 -W 3 8.8.8.8', { encoding: 'utf8', timeout: 90000 });
    pingOk = ping18 && !ping18.includes('100% packet loss');
  } catch {
    pingOk = false;
  }
  assert(pingOk, 'Internet works');

  // Test 20: Spent token
  console.log('\nTest 20: Reuse token (should fail)');
  const body20 = curlBody(`${API}/`, { method: 'POST', data: TEST_TOKEN });
  const json20 = body20 ? JSON.parse(body20) : null;
  assert(json20 && json20.kind === 21023, 'Returns kind=21023 for spent token');

  // Test 22: Session expiry
  console.log('\nTest 22: Session expiry (waiting 65s for allotment to expire)...');
  try {
    execSync(`echo '${sudoPw}' | sudo -S ip route add default via ${IP} dev wlp59s0 metric 50 2>/dev/null`, { encoding: 'utf8', timeout: 5000 });
  } catch {}
  await sleep(65000);
  let expiredPingOk = true;
  try {
    const ping22 = execSync('ping -c 2 -W 2 8.8.8.8', { encoding: 'utf8', timeout: 10000 });
    expiredPingOk = !ping22.includes('100% packet loss');
  } catch {
    expiredPingOk = false;
  }
  assert(!expiredPingOk, 'Internet blocked after session expiry');
  const body22 = curlBody(`${API}/usage`);
  assert(body22 && body22.includes('-1/-1'), 'Usage returns -1/-1 after expiry');

  // Test 23: Session renewal
  const TEST_TOKEN2 = process.env.TEST_TOKEN2;
  if (TEST_TOKEN2) {
    console.log('\nTest 23: Session renewal with second token');
    const body23 = curlBody(`${API}/`, { method: 'POST', data: TEST_TOKEN2 });
    const json23 = body23 ? JSON.parse(body23) : null;
    assert(json23 && json23.kind === 1022, 'Returns kind=1022 for renewal');
    await sleep(1500);
    let renewPingOk = false;
    try {
      const ping23 = execSync('ping -c 2 -W 2 8.8.8.8', { encoding: 'utf8', timeout: 10000 });
      renewPingOk = !ping23.includes('100% packet loss');
    } catch {
      renewPingOk = false;
    }
    assert(renewPingOk, 'Internet works after renewal');
  } else {
    console.log('\n  ⚠ Skipping test 23: Set TEST_TOKEN2 env var for renewal test');
  }
  try {
    execSync(`echo '${sudoPw}' | sudo -S ip route del default via ${IP} dev wlp59s0 metric 50 2>/dev/null`, { encoding: 'utf8', timeout: 5000 });
  } catch {}
} else {
  console.log('\n  ⚠ Skipping tests 16-20: Set TEST_TOKEN env var with a valid Cashu token');
}

// Test: whoami on :2121
console.log('\nTest: GET :2121/whoami');
const bodyWhoami = curlBody(`${API}/whoami`);
assert(bodyWhoami && bodyWhoami.includes('ip='), '/whoami returns ip=...');

// Test: Portal has payment form
console.log('\nTest: Portal has payment form');
const bodyPortal = curlBody(`http://${IP}/`);
assert(bodyPortal && bodyPortal.includes('cashuA'), 'Portal has Cashu token input');
assert(bodyPortal && bodyPortal.includes('Pay &amp; Connect') || bodyPortal && bodyPortal.includes('Pay'), 'Portal has Pay button');

// Summary
console.log(`\n=== Phase 2 Results: ${passed} passed, ${failed} failed ===\n`);
process.exit(failed > 0 ? 1 : 0);
