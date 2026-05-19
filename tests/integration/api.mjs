import { curl, curlBody, getPortalIP, canPing, canResolve, dnsResolvesToSelf } from './helpers/network.mjs';

const IP = getPortalIP();
let passed = 0, failed = 0;

function assert(condition, test) {
  if (condition) { console.log(`  ✓ ${test}`); passed++; }
  else { console.log(`  ✗ ${test}`); failed++; }
}

async function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

console.log(`\n=== API Tests (target: ${IP}) ===\n`);

// Test 3: Captive portal serves HTML
console.log('Test 3: GET / returns portal HTML');
const body3 = curlBody(`http://${IP}/`);
assert(body3 && body3.includes('TollGate'), 'Portal HTML contains "TollGate"');
assert(body3 && body3.includes('Pay & Connect'), 'Portal has Pay & Connect button');

// Test 4: Captive detection URIs
console.log('\nTest 4: Captive detection URIs (expect 200)');
for (const uri of ['/generate_204', '/hotspot-detect.html', '/canonical.html', '/success.txt', '/ncsi.txt', '/connecttest.txt', '/wpad.dat']) {
  const code = curl(`http://${IP}${uri}`);
  assert(code === '200', `${uri} → 200`);
}

// Test 4b: /redirect returns 302 to portal
console.log('\nTest 4b: /redirect → 302');
const redirectResp = curlBody(`http://${IP}/redirect`);
assert(redirectResp && redirectResp.includes('TollGate'), '/redirect reaches portal (via 302)');

// Test 7: /whoami (via API port for speed)
console.log('\nTest 7: GET /whoami');
const body7 = curlBody(`http://${IP}:2121/whoami`);
assert(body7 && body7.includes('ip='), '/whoami returns ip=...');

// Test 8: /usage (via API port)
console.log('\nTest 8: GET /usage');
const body8raw = curlBody(`http://${IP}:2121/usage`);
try {
  const usageJson = JSON.parse(body8raw);
  assert(usageJson && usageJson.activeSessions === 0, '/usage shows 0 sessions before auth');
} catch {
  assert(body8raw && body8raw.includes('-1/-1'), '/usage returns -1/-1 before auth');
}

// Test 5: DNS hijack before auth
console.log('\nTest 5: DNS hijack before auth');
assert(dnsResolvesToSelf('google.com'), 'DNS resolves google.com to AP IP');

// Test 6: No internet before auth
console.log('\nTest 6: No internet before auth');
assert(!canPing('8.8.8.8', 1), 'ping 8.8.8.8 fails before auth');

// Test 9: Grant access
console.log('\nTest 9: GET /grant_access');
const body9 = curlBody(`http://${IP}:2121/grant_access`);
assert(body9 && body9.includes('"granted"'), 'Grant access returns {"status":"granted"}');

await sleep(2000);

// Test 10: DNS forward after auth
console.log('\nTest 10: DNS forward after auth');
assert(canResolve('google.com'), 'DNS resolves normally after auth');

// Test 11: Internet after auth
console.log('\nTest 11: Internet after auth');
assert(canPing('8.8.8.8'), 'ping 8.8.8.8 succeeds after auth');

// Test 12: HTTP browsing works
console.log('\nTest 12: HTTP browsing');
const body12 = curlBody('http://example.com/');
assert(body12 && (body12.includes('Example Domain') || body12.includes('example')), 'HTTP page loads');

// Test 13: Reset auth
console.log('\nTest 13: GET /reset_authentication');
const body13 = curlBody(`http://${IP}:2121/reset_authentication`);
assert(body13 && body13.includes('"reset"'), 'Reset returns {"status":"reset"}');

await sleep(2000);

// Test 14: Internet blocked after reset
console.log('\nTest 14: Internet blocked after reset');
assert(!canPing('8.8.8.8', 1), 'ping fails after auth reset');

// Summary
console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
process.exit(failed > 0 ? 1 : 0);
