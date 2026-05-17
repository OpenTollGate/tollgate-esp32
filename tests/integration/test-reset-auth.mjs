import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.192.45.1';
const API = `http://${IP}:2121`;
const SUDO_PW = process.env.SUDO_PW || 'c03rad0r123';
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
  run('cashu -h https://testnut.cashu.space invoice ' + amount + ' 2>&1');
  const out = run('cashu -h https://testnut.cashu.space send --legacy ' + amount + ' 2>&1');
  const match = out && out.match(/cashuA[a-zA-Z0-9_-]+/);
  return match ? match[0] : null;
}

function canPing(host = '8.8.8.8') {
  const result = run(`ping -c 1 -W 2 -I wlp59s0 ${host} 2>/dev/null`);
  return result && !result.includes('100% packet loss');
}

console.log(`\n=== Reset Auth Integration Test (target: ${IP}) ===\n`);

console.log('1. Reset auth to clear state');
const reset1 = run(`curl -s --connect-timeout 10 http://${IP}/reset_authentication`);
assert(reset1 && reset1.includes('reset'), 'Reset returns {"status":"reset"}');

await sleep(1000);

console.log('\n2. Verify no session');
const usage1 = run(`curl -s --connect-timeout 10 ${API}/usage`);
assert(usage1 && usage1.includes('-1/-1'), 'Usage is -1/-1 before payment');

console.log('\n3. Verify internet blocked');
assert(!canPing(), 'Ping blocked before payment');

console.log('\n4. Pay with valid token');
const token = mintToken(21);
assert(token !== null, 'Token generated');
if (token) {
  const payResult = runJson(`curl -s --connect-timeout 20 -X POST --data-binary '${token}' -H "Content-Type: application/cashu" ${API}/`);
  assert(payResult && payResult.kind === 1022, 'Payment accepted (kind=1022)');
  const allotment = payResult && payResult.tags && payResult.tags.find(t => t[0] === 'allotment');
  assert(allotment && parseInt(allotment[1]) > 0, `Allotment: ${allotment ? allotment[1] : 'N/A'}ms`);
}

await sleep(1000);

console.log('\n5. Verify session active');
const usage2 = run(`curl -s --connect-timeout 10 ${API}/usage`);
assert(usage2 && !usage2.includes('-1/-1'), `Usage: ${usage2}`);

console.log('\n6. Verify internet allowed');
assert(canPing(), 'Ping works with active session');

console.log('\n7. Reset auth while session active');
const reset2 = run(`curl -s --connect-timeout 10 http://${IP}/reset_authentication`);
assert(reset2 && reset2.includes('reset'), 'Reset returns {"status":"reset"}');

await sleep(1000);

console.log('\n8. Verify session cleared');
const usage3 = run(`curl -s --connect-timeout 10 ${API}/usage`);
assert(usage3 && usage3.includes('-1/-1'), 'Usage is -1/-1 after reset');

console.log('\n9. Verify internet blocked again');
assert(!canPing(), 'Ping blocked after reset');

console.log('\n10. Pay again (new token)');
const token2 = mintToken(21);
if (token2) {
  const pay2 = runJson(`curl -s --connect-timeout 20 -X POST --data-binary '${token2}' -H "Content-Type: application/cashu" ${API}/`);
  assert(pay2 && pay2.kind === 1022, 'Second payment accepted');
}

await sleep(1000);

console.log('\n11. Verify internet works again');
assert(canPing(), 'Ping works with new session');

console.log('\n12. Final reset');
run(`curl -s --connect-timeout 10 http://${IP}/reset_authentication`);

console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
process.exit(failed > 0 ? 1 : 0);
