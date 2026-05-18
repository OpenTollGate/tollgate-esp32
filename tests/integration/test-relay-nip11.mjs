import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.192.45.1';
const RELAY_PORT = 4869;
const RELAY_URL = `http://${IP}:${RELAY_PORT}`;

let passed = 0, failed = 0;

function assert(condition, test) {
  if (condition) { console.log(`  \u2713 ${test}`); passed++; }
  else { console.log(`  \u2717 ${test}`); failed++; }
}

async function runTests() {
  console.log(`\n=== NIP-11 Relay Info Tests (target: ${RELAY_URL}) ===\n`);

  console.log('--- Test 1: NIP-11 info document ---');
  let nip11;
  try {
    nip11 = execSync(
      `curl -s --connect-timeout 5 -H "Accept: application/nostr+json" "${RELAY_URL}"`,
      { encoding: 'utf8', timeout: 8000 }
    );
  } catch (e) {
    console.log(`  Failed to fetch NIP-11: ${e.message}`);
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
    process.exit(1);
  }

  let doc;
  try {
    doc = JSON.parse(nip11);
    assert(true, 'NIP-11 returns valid JSON');
  } catch (e) {
    assert(false, `NIP-11 returns valid JSON — ${e.message}`);
    console.log(`  Response: ${nip11.substring(0, 200)}`);
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
    process.exit(1);
  }

  console.log('\n--- Test 2: Required NIP-11 fields ---');
  assert(typeof doc.name === 'string' && doc.name.length > 0, `name: "${doc.name}"`);
  assert(typeof doc.description === 'string' && doc.description.length > 0, `description present (${doc.description.length} chars)`);
  assert(typeof doc.software === 'string', `software: "${doc.software}"`);
  assert(typeof doc.version === 'string', `version: "${doc.version}"`);

  console.log('\n--- Test 3: NIP support ---');
  assert(Array.isArray(doc.supported_nips), 'supported_nips is array');
  if (Array.isArray(doc.supported_nips)) {
    assert(doc.supported_nips.includes(1), 'NIP-01 (basic protocol) supported');
    assert(doc.supported_nips.includes(9), 'NIP-09 (deletion) supported');
    assert(doc.supported_nips.includes(11), 'NIP-11 (relay info) supported');
    console.log(`  Supported NIPs: ${doc.supported_nips.join(', ')}`);
  }

  console.log('\n--- Test 4: TollGate-specific fields ---');
  if (doc.name) assert(doc.name.includes('TollGate') || doc.name.includes('4869'),
    'Name mentions TollGate or port 4869');
  if (doc.supported_nips && doc.limitation) {
    console.log(`  Limitations: ${JSON.stringify(doc.limitation)}`);
  }

  console.log('\n--- Test 5: HTTP without Accept header returns HTML (not NIP-11) ---');
  try {
    const html = execSync(
      `curl -s --connect-timeout 5 "${RELAY_URL}"`,
      { encoding: 'utf8', timeout: 8000 }
    );
    try {
      JSON.parse(html);
      assert(false, 'Without Accept header, returns non-JSON (HTML page)');
    } catch {
      assert(true, 'Without Accept header, returns non-JSON (HTML page)');
    }
  } catch (e) {
    assert(true, 'Without Accept header, returns non-JSON or empty');
  }

  console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
  process.exit(failed > 0 ? 1 : 0);
}

runTests().catch(e => {
  console.error('Test error:', e.message);
  process.exit(1);
});
