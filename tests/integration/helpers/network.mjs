import { execSync } from 'child_process';

const DEFAULT_IP = '10.192.45.1';
const WIFI_IFACE = 'wlp59s0';

export function getPortalIP() {
  return process.env.TOLLGATE_IP || DEFAULT_IP;
}

export function curl(url, timeout = 5) {
  try {
    return execSync(
      `curl -s -o /dev/null -w "%{http_code}" --connect-timeout ${timeout} --max-time ${timeout + 5} "${url}"`,
      { encoding: 'utf8', timeout: (timeout + 5) * 1000 }
    ).trim();
  } catch {
    return null;
  }
}

export function curlBody(url, timeout = 5) {
  try {
    return execSync(
      `curl -s --connect-timeout ${timeout} --max-time ${timeout + 5} "${url}"`,
      { encoding: 'utf8', timeout: (timeout + 5) * 1000 }
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

export function canResolve(domain, timeout = 3) {
  try {
    const result = execSync(
      `nslookup -timeout=${timeout} ${domain} 2>&1`,
      { encoding: 'utf8', timeout: (timeout + 2) * 1000 }
    );
    return result && result.includes('Address') && !result.includes('NXDOMAIN');
  } catch {
    return false;
  }
}

export function dnsResolvesToSelf(domain) {
  const ip = getPortalIP();
  try {
    const result = execSync(
      `nslookup ${domain} ${ip} 2>&1`,
      { encoding: 'utf8', timeout: 8000 }
    );
    return result && result.includes(ip);
  } catch {
    return false;
  }
}
