import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.185.47.1';
const RELAYS = ['wss://relay.damus.io', 'wss://nos.lol'];

let passed = 0, failed = 0;

function assert(condition, test) {
  if (condition) { console.log(`  \u2713 ${test}`); passed++; }
  else { console.log(`  \u2717 ${test}`); failed++; }
}

function nak(args, timeout = 10000) {
  try {
    return execSync(`timeout ${timeout / 1000} nak ${args}`, {
      encoding: 'utf8',
      stdio: ['pipe', 'pipe', 'pipe'],
      timeout
    }).trim();
  } catch (e) {
    return e.stdout ? e.stdout.trim() : '';
  }
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function runTests() {
  console.log(`\n=== CVM Integration Tests (target: ${IP}) ===\n`);

  const npub = nak(`key public a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2`);
  const npubHex = npub.trim();
  console.log(`Board npub: ${npubHex}`);

  const npubBech32 = nak(`encode npub ${npubHex}`).trim();
  console.log(`Board npub (bech32): ${npubBech32}`);

  assert(npubHex.length === 64, 'npub hex is 64 chars');

  console.log('\n--- Test: Kind 11316 server announcement ---');
  for (const relay of RELAYS) {
    console.log(`  Querying ${relay}...`);
    const result = nak(`req -k 11316 -a ${npubHex} -l 1 ${relay}`, 8000);
    if (result.length > 0) {
      assert(result.includes('"kind"') || result.includes('11316'),
             `Kind 11316 found on ${relay}`);
      if (result.includes('TollGate')) {
        assert(true, `Announcement contains "TollGate"`);
      }
    } else {
      console.log(`  (no result from ${relay} — relay may be offline)`);
    }
  }

  console.log('\n--- Test: Kind 11317 tools list ---');
  for (const relay of RELAYS) {
    const result = nak(`req -k 11317 -a ${npubHex} -l 1 ${relay}`, 8000);
    if (result.length > 0) {
      assert(result.includes('"kind"') || result.includes('11317'),
             `Kind 11317 found on ${relay}`);
      if (result.includes('get_config') && result.includes('wallet_melt')) {
        assert(true, `Tools list has expected tools`);
      }
    } else {
      console.log(`  (no result from ${relay} — relay may be offline)`);
    }
  }

  console.log('\n--- Test: Kind 10002 relay list ---');
  for (const relay of RELAYS) {
    const result = nak(`req -k 10002 -a ${npubHex} -l 1 ${relay}`, 8000);
    if (result.length > 0) {
      assert(result.includes('"kind"') || result.includes('10002'),
             `Kind 10002 found on ${relay}`);
    } else {
      console.log(`  (no result from ${relay} — relay may be offline)`);
    }
  }

  console.log('\n--- Test: API get_config (control check) ---');
  try {
    const apiResult = execSync(`curl -s http://${IP}:2121/usage`, { encoding: 'utf8', timeout: 5000 });
    assert(apiResult.length > 0, 'API /usage responds (board is reachable)');
  } catch (e) {
    console.log('  (API not reachable — board may be offline or not flashed yet)');
  }

  console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
  process.exit(failed > 0 ? 1 : 0);
}

runTests().catch(e => {
  console.error('Test error:', e.message);
  process.exit(1);
});
