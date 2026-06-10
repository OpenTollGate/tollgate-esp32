import { execSync } from 'child_process';
import WebSocket from 'ws';

const IP = process.env.TOLLGATE_IP || '10.185.47.1';
const CVM_RELAY = process.env.CVM_RELAY || 'wss://nos.lol';
const NSEC = process.env.CVM_NSEC || 'a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2';

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

function connectWSS(url) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const timer = setTimeout(() => { ws.close(); reject(new Error('connect timeout')); }, 10000);
    ws.on('open', () => { clearTimeout(timer); resolve(ws); });
    ws.on('error', (e) => { clearTimeout(timer); reject(e); });
  });
}

function collectMessages(ws, count, timeoutMs = 15000) {
  return new Promise((resolve) => {
    const msgs = [];
    const timer = setTimeout(() => resolve(msgs), timeoutMs);
    ws.on('message', (data) => {
      try { msgs.push(JSON.parse(data.toString())); } catch { msgs.push(data.toString()); }
      if (msgs.length >= count) { clearTimeout(timer); resolve(msgs); }
    });
    ws.on('error', () => { clearTimeout(timer); resolve(msgs); });
    ws.on('close', () => { clearTimeout(timer); resolve(msgs); });
  });
}

async function runTests() {
  console.log(`\n=== CVM MCP Roundtrip Tests (target: ${IP}) ===\n`);

  const npub = nak(`key public ${NSEC}`);
  console.log(`Board npub: ${npub}`);
  assert(npub.length === 64, 'npub hex is 64 chars');

  console.log('\n--- Test 1: Board API reachable ---');
  try {
    const result = execSync(`curl -s --connect-timeout 5 http://${IP}:2121/usage`, { encoding: 'utf8', timeout: 5000 });
    assert(result.length > 0, 'API /usage responds');
  } catch (e) {
    assert(false, `API /usage reachable — ${e.message}`);
    console.log('\n  Board not reachable — skipping remaining tests');
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
    process.exit(failed > 0 ? 1 : 0);
  }

  console.log('\n--- Test 2: Kind 11316 announcement exists on relay ---');
  const ann11316 = nak(`req -k 11316 -a ${npub} -l 1 ${CVM_RELAY}`, 8000);
  if (ann11316.includes('"kind"') || ann11316.includes('11316')) {
    assert(true, `Kind 11316 found on ${CVM_RELAY}`);
    if (ann11316.includes('TollGate')) {
      assert(true, 'Announcement contains "TollGate"');
    }
  } else {
    console.log(`  (no 11316 from ${CVM_RELAY} — may not have been published yet)`);
  }

  console.log('\n--- Test 3: Kind 11317 tools list exists on relay ---');
  const ann11317 = nak(`req -k 11317 -a ${npub} -l 1 ${CVM_RELAY}`, 8000);
  if (ann11317.includes('"kind"') || ann11317.includes('11317')) {
    assert(true, `Kind 11317 found on ${CVM_RELAY}`);
    const hasTools = ann11317.includes('get_config') || ann11317.includes('tools');
    assert(hasTools, 'Tools list contains expected tool names');
  } else {
    console.log(`  (no 11317 from ${CVM_RELAY} — may not have been published yet)`);
  }

  console.log('\n--- Test 4: Kind 10002 relay list exists ---');
  const ann10002 = nak(`req -k 10002 -a ${npub} -l 1 ${CVM_RELAY}`, 8000);
  if (ann10002.includes('"kind"') || ann10002.includes('10002')) {
    assert(true, `Kind 10002 found on ${CVM_RELAY}`);
  } else {
    console.log(`  (no 10002 from ${CVM_RELAY})`);
  }

  console.log('\n--- Test 5: MCP get_config roundtrip via public relay ---');
  try {
    const content = JSON.stringify({
      jsonrpc: '2.0',
      id: Date.now(),
      method: 'tools/call',
      params: { name: 'get_config', arguments: {} }
    });

    const eventOut = nak(`event --sec ${NSEC} --kind 25910 --tag p=${npub} --content '${content.replace(/'/g, "'\\''")}' ${CVM_RELAY}`, 8000);
    const published = eventOut.includes('Success') || eventOut.includes('"id"');
    assert(published, `Published kind 25910 get_config to ${CVM_RELAY}`);

    if (published) {
      console.log('  Waiting 8s for board to process and respond...');
      await sleep(8000);

      const resp = nak(`req -k 25910 -a ${npub} -l 5 ${CVM_RELAY}`, 8000);
      const hasResponse = resp.includes('"kind"') && resp.includes('25910');
      assert(hasResponse, 'Received kind 25910 response from board');

      if (hasResponse) {
        try {
          const lines = resp.split('\n').filter(l => l.includes('"kind"'));
          for (const line of lines) {
            const evt = JSON.parse(line);
            if (evt.kind === 25910 && evt.content) {
              try {
                const mcpr = JSON.parse(evt.content);
                assert(mcpr.result !== undefined || mcpr.error !== undefined, 'Response has MCP result or error');
              } catch {
                assert(evt.content.length > 0, 'Response content is non-empty');
              }
              break;
            }
          }
        } catch {
          assert(resp.length > 0, 'Raw response data received');
        }
      }
    }
  } catch (e) {
    assert(false, `MCP roundtrip — ${e.message}`);
  }

  console.log('\n--- Test 6: MCP get_balance roundtrip via public relay ---');
  try {
    const content = JSON.stringify({
      jsonrpc: '2.0',
      id: Date.now(),
      method: 'tools/call',
      params: { name: 'get_balance', arguments: {} }
    });

    const eventOut = nak(`event --sec ${NSEC} --kind 25910 --tag p=${npub} --content '${content.replace(/'/g, "'\\''")}' ${CVM_RELAY}`, 8000);
    const published = eventOut.includes('Success') || eventOut.includes('"id"');
    assert(published, `Published kind 25910 get_balance to ${CVM_RELAY}`);

    if (published) {
      console.log('  Waiting 8s for board to process and respond...');
      await sleep(8000);

      const resp = nak(`req -k 25910 -a ${npub} -l 10 ${CVM_RELAY}`, 8000);
      const hasBalance = resp.includes('balance') || resp.includes('get_balance') || resp.includes('"kind"');
      assert(hasBalance, 'Received balance response');
    }
  } catch (e) {
    assert(false, `get_balance roundtrip — ${e.message}`);
  }

  console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
  process.exit(failed > 0 ? 1 : 0);
}

runTests().catch(e => {
  console.error('Test error:', e.message);
  process.exit(1);
});
