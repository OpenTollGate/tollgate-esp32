import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.192.45.1';
let passed = 0, failed = 0;

function assert(condition, test) {
  if (condition) { console.log(`  ✓ ${test}`); passed++; }
  else { console.log(`  ✗ ${test}`); failed++; }
}

function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 15000 }); }
  catch { return null; }
}

console.log(`\n=== Network Tests (target: ${IP}) ===\n`);

// Test 1: AP visible in scan
console.log('Test 1: AP visible in scan');
const scan = run('nmcli -t -f SSID dev wifi list 2>/dev/null');
assert(scan && scan.includes('TollGate'), 'TollGate SSID visible in WiFi scan');

// Test 2: DHCP lease
console.log('\nTest 2: DHCP lease / connectivity');
const ip_show = run(`ip addr show | grep "inet ${IP.split('.').slice(0,3).join('.')}"`);
assert(ip_show !== null, `Has IP in ${IP.split('.').slice(0,3).join('.')}.* subnet`);

// Test 5: DNS hijack
console.log('\nTest 5: DNS hijack before auth');
const ns1 = run(`nslookup random-test.example.com ${IP} 2>/dev/null`);
assert(ns1 && ns1.includes(IP), 'DNS resolves arbitrary domain to AP IP');

// Test 6: No internet
console.log('\nTest 6: No internet before auth');
const ping1 = run('ping -c 1 -W 3 1.1.1.1 2>/dev/null');
assert(ping1 === null || ping1.includes('100% packet loss'), 'Internet blocked before auth');

// Grant access for further tests
console.log('\nGranting access...');
run(`curl -s http://${IP}/grant_access`);

import { execSync as exec } from 'child_process';
await new Promise(r => setTimeout(r, 2000));

// Test 10: DNS forward
console.log('Test 10: DNS forward after auth');
const ns2 = run(`nslookup google.com ${IP} 2>/dev/null`);
assert(ns2 && !ns2.includes(IP) && ns2.includes('Address'), 'DNS resolves to real IPs');

// Test 11: Internet
console.log('\nTest 11: Internet after auth');
const ping2 = run('ping -c 2 -W 3 8.8.8.8');
assert(ping2 && !ping2.includes('100% packet loss'), 'ping succeeds after auth');

// Reset
console.log('\nResetting auth...');
run(`curl -s http://${IP}/reset_authentication`);
await new Promise(r => setTimeout(r, 2000));

// Test 14
console.log('Test 14: Internet blocked after reset');
const ping3 = run('ping -c 1 -W 3 8.8.8.8 2>/dev/null');
assert(ping3 === null || ping3.includes('100% packet loss'), 'Internet blocked after reset');

console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
process.exit(failed > 0 ? 1 : 0);
