import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.185.47.1';
const API = `http://${IP}:2121`;
let passed = 0, failed = 0;

function assert(cond, msg) {
  if (cond) { console.log(`  ✓ ${msg}`); passed++; }
  else { console.log(`  ✗ ${msg}`); failed++; }
}

function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 15000 }); }
  catch { return null; }
}

function runJson(cmd) {
  const out = run(cmd);
  try { return out ? JSON.parse(out) : null; }
  catch { return null; }
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function mintToken(amount = 21) {
  run('cashu -h https://testnut.cashu.exchange invoice ' + amount + ' 2>&1');
  const out = run('cashu -h https://testnut.cashu.exchange send --legacy ' + amount + ' 2>&1');
  const match = out && out.match(/cashuA[a-zA-Z0-9_-]+/);
  return match ? match[0] : null;
}

function canPing(host = '8.8.8.8') {
  const result = run(`ping -c 1 -W 2 -I wlp59s0 ${host} 2>/dev/null`);
  return result && !result.includes('100% packet loss');
}

console.log(`\n=== Session Expiry Integration Test (target: ${IP}) ===`);
console.log(`NOTE: This test waits 65s for session expiry. Total runtime ~80s.\n`);

console.log('1. Reset auth');
run(`curl -s --connect-timeout 10 http://${IP}/reset_authentication`);

await sleep(1000);

console.log('\n2. Verify blocked before payment');
assert(!canPing(), 'Ping blocked before payment');

const usage0 = run(`curl -s --connect-timeout 10 ${API}/usage`);
assert(usage0 && usage0.includes('-1/-1'), 'Usage is -1/-1');

console.log('\n3. Pay with valid token (21 sats = 60000ms)');
const token = mintToken(21);
assert(token !== null, 'Token generated');
if (token) {
  const payResult = runJson(`curl -s --connect-timeout 20 -X POST --data-binary '${token}' -H "Content-Type: application/cashu" ${API}/`);
  assert(payResult && payResult.kind === 1022, 'Payment accepted');
}

await sleep(1000);

console.log('\n4. Verify session active');
const usage1 = run(`curl -s --connect-timeout 10 ${API}/usage`);
assert(usage1 && !usage1.includes('-1/-1'), `Usage: ${usage1}`);

console.log('\n5. Verify internet works');
assert(canPing(), 'Ping works with active session');

const httpResult = run(`curl -s --connect-timeout 10 -m 10 --interface wlp59s0 http://1.1.1.1/ 2>/dev/null`);
assert(httpResult && httpResult.length > 0, 'HTTP request reaches internet');

console.log('\n6. Waiting 65s for session expiry (allotment=60000ms)...');
for (let i = 65; i > 0; i -= 5) {
  process.stdout.write(`\r  ${i}s remaining...`);
  await sleep(Math.min(5000, i * 1000));
}
console.log('\r  Session should be expired now.         ');

console.log('\n7. Verify session expired');
const usage2 = run(`curl -s --connect-timeout 10 ${API}/usage`);
assert(usage2 && usage2.includes('-1/-1'), `Usage after expiry: ${usage2}`);

console.log('\n8. Verify internet blocked after expiry');
assert(!canPing(), 'Ping blocked after session expiry');

const httpResult2 = run(`curl -s --connect-timeout 5 -m 5 --interface wlp59s0 http://1.1.1.1/ 2>/dev/null`);
assert(!httpResult2 || httpResult2.length === 0, 'HTTP blocked after expiry');

console.log('\n9. Pay again to verify renewal works');
const token2 = mintToken(21);
if (token2) {
  const pay2 = runJson(`curl -s --connect-timeout 20 -X POST --data-binary '${token2}' -H "Content-Type: application/cashu" ${API}/`);
  assert(pay2 && pay2.kind === 1022, 'Renewal payment accepted');
}

await sleep(1000);

console.log('\n10. Verify internet works after renewal');
assert(canPing(), 'Ping works after renewal');

run(`curl -s --connect-timeout 10 http://${IP}/reset_authentication`);

console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
process.exit(failed > 0 ? 1 : 0);
