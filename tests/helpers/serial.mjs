import { SerialPort } from 'serialport';
import { ReadlineParser } from '@serialport/parser-readline';
import { execSync } from 'child_process';

const DEFAULT_BAUD = 115200;
const BOOT_TIMEOUT = 30000;

export async function execSerial(portPath, command, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path: portPath, baudRate: DEFAULT_BAUD });
    const parser = port.pipe(new ReadlineParser());
    const lines = [];
    let resolved = false;

    const timer = setTimeout(() => {
      if (!resolved) { resolved = true; port.close(); resolve(lines.join('\n')); }
    }, timeoutMs);

    parser.on('data', (line) => {
      lines.push(line);
      if (line.includes('___END___') && !resolved) {
        resolved = true;
        clearTimeout(timer);
        port.close();
        resolve(lines.join('\n'));
      }
    });

    port.on('open', () => {
      port.write(command + '\n');
    });

    port.on('error', (err) => {
      if (!resolved) { resolved = true; clearTimeout(timer); reject(err); }
    });
  });
}

export async function waitForBoot(portPath, timeoutMs = BOOT_TIMEOUT) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path: portPath, baudRate: DEFAULT_BAUD });
    const parser = port.pipe(new ReadlineParser());
    const timer = setTimeout(() => {
      port.close();
      reject(new Error('Boot timeout'));
    }, timeoutMs);

    parser.on('data', (line) => {
      if (line.includes('TollGate services started') || line.includes('WiFi AP+STA started')) {
        clearTimeout(timer);
        setTimeout(() => { port.close(); resolve(true); }, 500);
      }
    });

    port.on('error', (err) => {
      clearTimeout(timer);
      reject(err);
    });
  });
}

export async function readSerial(portPath, durationMs = 3000) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path: portPath, baudRate: DEFAULT_BAUD });
    const parser = port.pipe(new ReadlineParser());
    const lines = [];

    const timer = setTimeout(() => {
      port.close();
      resolve(lines.join('\n'));
    }, durationMs);

    parser.on('data', (line) => lines.push(line));
    port.on('error', (err) => { clearTimeout(timer); reject(err); });
  });
}

export function resetDevice(portPath) {
  try {
    execSync(`python3 -m esptool --port ${portPath} run 2>/dev/null`, { timeout: 5000 });
  } catch {}
}
