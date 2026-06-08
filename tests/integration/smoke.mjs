import { execSync } from 'child_process';

const PORT = process.argv[2] || '/dev/ttyACM0';
const IP = process.env.TOLLGATE_IP || '10.185.47.1';
const SSID = process.env.AP_SSID || 'TollGate';

console.log(`\n=== Smoke Test (30s) ===`);
console.log(`Port: ${PORT}, Portal IP: ${IP}, SSID: ${SSID}\n`);

let passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { console.log(`  ✓ ${msg}`); passed++; }
  else { console.log(`  ✗ ${msg}`); failed++; }
}

function run(cmd) {
  try { return execSync(cmd, { encoding: 'utf8', timeout: 10000 }); }
  catch { return null; }
}

// 1. Check AP visible
const scan = run('nmcli -t -f SSID dev wifi list 2>/dev/null');
assert(scan && scan.includes(SSID), `SSID "${SSID}" visible`);

// 2. Check we can reach portal
const portal = run(`curl -s --connect-timeout 5 http://${IP}/`);
assert(portal && portal.includes('TollGate'), 'Portal HTML loads');

// 3. Grant access
const grant = run(`curl -s http://${IP}/grant_access`);
assert(grant && grant.includes('granted'), 'Grant access works');

// Wait for DNS
const sleep = ms => new Promise(r => setTimeout(r, ms));
await sleep(2000);

// 4. Internet works
const ping = run('ping -c 1 -W 3 -I wlp59s0 1.1.1.1 2>/dev/null');
assert(ping && !ping.includes('100% packet loss'), 'Internet works after grant');

// 5. Reset
const reset = run(`curl -s http://${IP}/reset_authentication`);
assert(reset && reset.includes('reset'), 'Reset auth works');

await sleep(2000);

// 6. Internet blocked
const ping2 = run('ping -c 1 -W 3 -I wlp59s0 1.1.1.1 2>/dev/null');
assert(!ping2 || ping2.includes('100% packet loss'), 'Internet blocked after reset');

console.log(`\n=== Smoke: ${passed} passed, ${failed} failed ===\n`);
process.exit(failed > 0 ? 1 : 0);
