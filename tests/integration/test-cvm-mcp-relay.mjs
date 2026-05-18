import { execSync } from 'child_process';

const IP = process.env.TOLLGATE_IP || '10.192.45.1';
const RELAY = 'wss://relay.primal.net';
const OWNER_NSEC = 'a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2';

let passed = 0, failed = 0;

function assert(condition, test) {
  if (condition) { console.log(`  \u2713 ${test}`); passed++; }
  else { console.log(`  \u2717 ${test}`); failed++; }
}

function nak(args, timeout = 15000) {
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

function generateId() {
  return Math.floor(Math.random() * 100000);
}

async function sendMcpRequest(nsec, method, params, timeout = 15000) {
  const id = generateId();
  const content = JSON.stringify({ jsonrpc: '2.0', id, method, params: params || {} });

  const boardNpub = nak(`key public ${OWNER_NSEC}`);

  const result = nak(
    `event -k 25910 -c '${content.replace(/'/g, "'\\''")}' -p ${boardNpub} --sec ${nsec} ${RELAY}`,
    timeout
  );

  const reqEventId = result.match(/"id":"([a-f0-9]{64})"/);
  return { reqEventId: reqEventId ? reqEventId[1] : null, id, content };
}

async function waitForResponse(reqEventId, timeout = 20000) {
  const start = Date.now();
  while (Date.now() - start < timeout) {
    const result = nak(
      `req -k 25910 -l 5 --tag e=${reqEventId} ${RELAY}`,
      8000
    );
    if (result.length > 0 && result.includes('"id"')) {
      return result;
    }
    await sleep(2000);
  }
  return null;
}

function extractMcpContent(rawEvent) {
  try {
    const lines = rawEvent.split('\n').filter(l => l.trim().startsWith('{'));
    for (const line of lines) {
      try {
        const event = JSON.parse(line);
        if (event.content) {
          return JSON.parse(event.content);
        }
      } catch {}
    }
  } catch {}
  return null;
}

async function runTests() {
  console.log(`\n=== CVM MCP Relay Integration Tests ===`);
  console.log(`Target: ${IP}`);
  console.log(`Relay: ${RELAY}\n`);

  const boardNpub = nak(`key public ${OWNER_NSEC}`);
  console.log(`Board npub: ${boardNpub}`);
  assert(boardNpub.length === 64, 'Board npub derived correctly');

  console.log('\n--- Pre-flight: Board is reachable ---');
  try {
    const apiResult = execSync(`curl -s --connect-timeout 5 http://${IP}:2121/usage`, { encoding: 'utf8', timeout: 10000 });
    assert(apiResult.length > 0, 'API /usage responds (board online)');
  } catch (e) {
    console.log('  WARNING: Board API not reachable. Tests may fail.');
  }

  console.log('\n--- Test 1: MCP initialize via relay ---');
  {
    const { reqEventId } = await sendMcpRequest(OWNER_NSEC, 'initialize');
    assert(reqEventId !== null, 'Initialize request published');
    if (reqEventId) {
      const resp = await waitForResponse(reqEventId);
      if (resp) {
        assert(resp.includes('protocolVersion'), 'Response has protocolVersion');
        assert(resp.includes('TollGate'), 'Response has server name TollGate');
      } else {
        assert(false, 'Got response for initialize');
      }
    }
  }

  console.log('\n--- Test 2: get_sessions via relay ---');
  {
    const { reqEventId } = await sendMcpRequest(OWNER_NSEC, 'tools/call', { name: 'get_sessions' });
    assert(reqEventId !== null, 'get_sessions request published');
    if (reqEventId) {
      const resp = await waitForResponse(reqEventId);
      if (resp) {
        assert(resp.includes('content'), 'Response has content');
        const mcp = extractMcpContent(resp);
        if (mcp && mcp.result && mcp.result.content) {
          const text = mcp.result.content[0]?.text;
          if (text) {
            const sessions = JSON.parse(text);
            assert(Array.isArray(sessions), 'get_sessions returns array');
            console.log(`    Active sessions: ${sessions.filter(s => s.active).length}`);
          }
        } else {
          assert(false, 'get_sessions MCP response parseable');
        }
      } else {
        assert(false, 'Got response for get_sessions');
      }
    }
  }

  console.log('\n--- Test 3: get_usage via relay ---');
  {
    const { reqEventId } = await sendMcpRequest(OWNER_NSEC, 'tools/call', { name: 'get_usage' });
    assert(reqEventId !== null, 'get_usage request published');
    if (reqEventId) {
      const resp = await waitForResponse(reqEventId);
      if (resp) {
        assert(resp.includes('content'), 'Response has content');
        const mcp = extractMcpContent(resp);
        if (mcp && mcp.result && mcp.result.content) {
          const text = mcp.result.content[0]?.text;
          if (text) {
            const usage = JSON.parse(text);
            assert(usage.metric !== undefined, 'get_usage returns metric');
            assert(usage.price_per_step !== undefined, 'get_usage returns price_per_step');
            assert(usage.step_size_ms !== undefined, 'get_usage returns step_size_ms');
            console.log(`    metric=${usage.metric}, price=${usage.price_per_step}, step=${usage.step_size_ms}ms`);
          }
        } else {
          assert(false, 'get_usage MCP response parseable');
        }
      } else {
        assert(false, 'Got response for get_usage');
      }
    }
  }

  console.log('\n--- Test 4: Non-owner auth rejection ---');
  {
    const throwawayNsec = nak('key generate');
    const throwawayNpub = nak(`key public ${throwawayNsec}`);
    console.log(`  Throwaway npub: ${throwawayNpub.substring(0, 16)}...`);
    assert(throwawayNsec.length === 64, 'Generated throwaway nsec');

    const { reqEventId } = await sendMcpRequest(throwawayNsec, 'tools/call', { name: 'get_config' });
    assert(reqEventId !== null, 'Non-owner request published');
    if (reqEventId) {
      const resp = await waitForResponse(reqEventId, 12000);
      if (resp && resp.includes('"id"')) {
        assert(false, 'Non-owner should NOT get a response');
      } else {
        assert(true, 'Non-owner correctly ignored (no response)');
      }
    }

    await sleep(2000);

    console.log('  Control: sending owner request to verify board still responsive...');
    const { reqEventId: ctrlId } = await sendMcpRequest(OWNER_NSEC, 'tools/call', { name: 'get_config' });
    if (ctrlId) {
      const ctrlResp = await waitForResponse(ctrlId);
      if (ctrlResp) {
        assert(true, 'Owner control request after rejection test: PASS');
      } else {
        assert(false, 'Owner control request should get response');
      }
    }
  }

  console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
  process.exit(failed > 0 ? 1 : 0);
}

runTests().catch(e => {
  console.error('Test error:', e.message);
  process.exit(1);
});
