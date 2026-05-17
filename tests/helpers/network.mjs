import { execSync } from 'child_process';

const ESP32_IP = process.env.TOLLGATE_IP || '10.192.45.1';
const TIMEOUT = 5000;

export function curl(args, expectStatus = null) {
  const cmd = `curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time ${TIMEOUT/1000} ${args}`;
  try {
    const result = execSync(cmd, { encoding: 'utf8', timeout: TIMEOUT + 2000 }).trim();
    if (expectStatus && result !== String(expectStatus)) {
      throw new Error(`Expected HTTP ${expectStatus}, got ${result}`);
    }
    return result;
  } catch (e) {
    if (e.status === 'ETIMEDOUT' || e.killed) return 'TIMEOUT';
    throw e;
  }
}

export function curlBody(url) {
  const cmd = `curl -s --connect-timeout 5 --max-time ${TIMEOUT/1000} "${url}"`;
  try {
    return execSync(cmd, { encoding: 'utf8', timeout: TIMEOUT + 2000 });
  } catch {
    return null;
  }
}

export function getPortalIP() { return ESP32_IP; }

export function canPing(host = '8.8.8.8', count = 2) {
  try {
    const result = execSync(`ping -c ${count} -W 2 -I wlp59s0 ${host}`, { encoding: 'utf8', timeout: 10000 });
    return result.includes('0% packet loss') || result.includes('1 packets transmitted');
  } catch {
    return false;
  }
}

export function canResolve(domain = 'google.com') {
  try {
    const result = execSync(`nslookup ${domain} ${ESP32_IP}`, { encoding: 'utf8', timeout: 10000 });
    return result.includes('Address') && !result.includes('NXDOMAIN');
  } catch (e) {
    const result = e.stdout || '';
    return result.includes('Address') && !result.includes('NXDOMAIN');
  }
}

export function dnsResolvesToSelf(domain = 'google.com') {
  try {
    const result = execSync(`nslookup ${domain} ${ESP32_IP}`, { encoding: 'utf8', timeout: 10000 });
    return result.includes(ESP32_IP);
  } catch (e) {
    return e.stdout && e.stdout.includes(ESP32_IP);
  }
}

export function connectToAP(ssid, password = '') {
  try {
    if (password) {
      execSync(`nmcli dev wifi connect "${ssid}" password "${password}" ifname wlan0`, { timeout: 30000 });
    } else {
      execSync(`nmcli dev wifi connect "${ssid}" ifname wlan0`, { timeout: 30000 });
    }
    return true;
  } catch {
    return false;
  }
}

export function disconnectAP() {
  try {
    execSync('nmcli dev disconnect wlan0 2>/dev/null || true', { timeout: 10000 });
    return true;
  } catch {
    return false;
  }
}

export function getWifiInterface() {
  try {
    const result = execSync('nmcli -t -f DEVICE,TYPE dev status', { encoding: 'utf8' });
    const line = result.split('\n').find(l => l.includes('wifi'));
    return line ? line.split(':')[0] : null;
  } catch {
    return null;
  }
}
