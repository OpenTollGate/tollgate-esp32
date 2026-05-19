import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.192.45.1';
const API = `http://${IP}:2121`;
let passed = 0, failed = 0;

function assert(cond, msg) {
  if (cond) { console.log(`  ✓ ${msg}`); passed++; }
  else { console.log(`  ✗ ${msg}`); failed++; }
}

function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 90000 }); }
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

function dnsResolves(domain, server) {
  const result = run(`dig +short +timeout=5 ${domain} @${server} 2>&1`);
  return result && result.trim().length > 0 && !result.includes('NXDOMAIN');
}

function dnsResolvesToSelf(domain) {
  try {
    const result = run(`dig +short +timeout=5 ${domain} @${IP} 2>&1`);
    return result && result.trim() === IP;
  } catch {
    return false;
  }
}

function canPing(host = '8.8.8.8') {
  const result = run(`ping -c 1 -W 2 -I wlp59s0 ${host} 2>/dev/null`);
  return result && !result.includes('100% packet loss');
}

console.log(`\n=== DNS + Firewall Integration Test (target: ${IP}) ===\n`);

console.log('0. Pre-minting token (need internet for cashu CLI)');
const preToken = mintToken(21);
assert(preToken !== null, 'Pre-minted token');

console.log('\n--- Part 1: Before Authentication ---\n');

console.log('1. DNS hijack: resolves to ESP32 AP IP');
assert(dnsResolvesToSelf('google.com'), 'google.com resolves to AP IP');
assert(dnsResolvesToSelf('random-test.example.com'), 'random domain resolves to AP IP');

console.log('\n2. DNS hijack: upstream DNS not reachable through ESP32');
const upstreamResolve = run(`dig +short +timeout=5 google.com @${IP} 2>&1`);
assert(!upstreamResolve || upstreamResolve.trim() === IP || upstreamResolve.trim().length === 0, 'DNS queries hijacked (not forwarded upstream) before auth');

console.log('\n3. Per-client NAT filter: ping blocked');
assert(!canPing(), 'Ping to 8.8.8.8 blocked by NAT filter');

console.log('\n4. Per-client NAT filter: HTTP blocked');
const httpBefore = run(`curl -s --connect-timeout 5 -m 5 --interface wlp59s0 http://1.1.1.1/ 2>/dev/null`);
assert(!httpBefore || httpBefore.length === 0, 'HTTP blocked before auth');

console.log('\n5. Captive portal and API still accessible');
const portal = run(`curl -s --connect-timeout 30 --max-time 60 http://${IP}/`);
assert(portal && portal.includes('TollGate'), 'Portal HTML accessible');
const apiDisc = runJson(`curl -s --connect-timeout 30 --max-time 60 ${API}/`);
assert(apiDisc && apiDisc.kind === 10021, 'API discovery accessible');

console.log('\n--- Part 2: After Authentication ---\n');

console.log('6. Reset + Pay');
run(`curl -s --connect-timeout 10 --max-time 20 ${API}/reset_authentication`);
await sleep(1000);

assert(preToken !== null, 'Token available');
if (preToken) {
  const payResult = runJson(`curl -s --connect-timeout 20 -X POST --data-binary '${preToken}' -H "Content-Type: application/cashu" ${API}/`);
  assert(payResult && payResult.kind === 1022, 'Payment accepted');
}

await sleep(1000);

console.log('\n7. DNS now forwards to upstream');
assert(dnsResolveWorks('google.com'), 'DNS resolves to real IPs after auth');

console.log('\n8. Per-client NAT filter: ping allowed');
assert(canPing(), 'Ping to 8.8.8.8 allowed after auth');

console.log('\n9. Per-client NAT filter: HTTP allowed');
const httpAfter = run(`curl -s --connect-timeout 10 -m 10 --interface wlp59s0 http://1.1.1.1/ 2>/dev/null`);
assert(httpAfter && httpAfter.length > 0, 'HTTP allowed after auth');

console.log('\n--- Part 3: After Revocation ---\n');

console.log('10. Reset auth');
run(`curl -s --connect-timeout 10 --max-time 20 ${API}/reset_authentication`);
await sleep(3000);

console.log('\n11. DNS goes back to hijack');
assert(dnsResolvesToSelf('google.com'), 'DNS hijack restored after revoke');

console.log('\n12. Per-client NAT filter: ping blocked again');
assert(!canPing(), 'Ping blocked after revoke');

console.log('\n13. Per-client NAT filter: HTTP blocked again');
const httpRevoke = run(`curl -s --connect-timeout 5 -m 5 --interface wlp59s0 http://1.1.1.1/ 2>/dev/null`);
assert(!httpRevoke || httpRevoke.length === 0, 'HTTP blocked after revoke');

function dnsResolveWorks(domain) {
  const result = run(`dig +short +timeout=5 ${domain} @${IP} 2>&1`);
  return result && result.trim().length > 0 && result.trim() !== IP && !result.includes('NXDOMAIN');
}

console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
process.exit(failed > 0 ? 1 : 0);
