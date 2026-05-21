import { execSync } from 'child_process';

const DEFAULT_IP = '10.192.45.1';
const WIFI_IFACE = 'wlp59s0';

export function getPortalIP() {
  return process.env.TOLLGATE_IP || DEFAULT_IP;
}

export function curl(url, timeout = 30) {
  try {
    return execSync(
      `curl -s -o /dev/null -w "%{http_code}" --connect-timeout ${timeout} --max-time ${timeout + 10} "${url}"`,
      { encoding: 'utf8', timeout: (timeout + 10) * 1000 }
    ).trim();
  } catch {
    return null;
  }
}

export function curlBody(url, timeout = 30) {
  try {
    return execSync(
      `curl -s --connect-timeout ${timeout} --max-time ${timeout + 10} "${url}"`,
      { encoding: 'utf8', timeout: (timeout + 10) * 1000 }
    );
  } catch {
    return null;
  }
}

export function canPing(host = '8.8.8.8', count = 1) {
  try {
    const result = execSync(
      `ping -c ${count} -W 3 -I ${WIFI_IFACE} ${host} 2>/dev/null`,
      { encoding: 'utf8', timeout: 10000 }
    );
    return result && !result.includes('100% packet loss');
  } catch {
    return false;
  }
}

export function canResolve(domain, timeout = 5) {
  try {
    const result = execSync(
      `dig +short +timeout=${timeout} +tries=1 ${domain} 2>&1`,
      { encoding: 'utf8', timeout: (timeout + 2) * 1000 }
    ).trim();
    return result.length > 0 && !result.includes('NXDOMAIN');
  } catch {
    return false;
  }
}

export function dnsResolvesToSelf(domain) {
  const ip = getPortalIP();
  try {
    const result = execSync(
      `dig +short +timeout=5 ${domain} @${ip} 2>&1`,
      { encoding: 'utf8', timeout: 10000 }
    ).trim();
    return result === ip;
  } catch {
    return false;
  }
}
