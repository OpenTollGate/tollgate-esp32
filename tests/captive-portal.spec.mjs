import { test, expect } from '@playwright/test';

const PORTAL_IP = process.env.TOLLGATE_IP || '192.168.4.1';
const PORTAL_URL = `http://${PORTAL_IP}`;

test.describe('Captive Portal - Phase 1', () => {

  test('portal page loads with TollGate branding', async ({ page }) => {
    await page.goto(PORTAL_URL);
    await expect(page.locator('h1')).toHaveText('TollGate');
    await expect(page.locator('.subtitle')).toContainText('internet access');
  });

  test('portal shows price', async ({ page }) => {
    await page.goto(PORTAL_URL);
    const priceEl = page.locator('.price-amount');
    await expect(priceEl).not.toBeEmpty({ timeout: 5000 });
  });

  test('grant access button exists', async ({ page }) => {
    await page.goto(PORTAL_URL);
    const btn = page.locator('#grantBtn');
    await expect(btn).toBeVisible();
    await expect(btn).toHaveText(/Grant Free Access/i);
  });

  test('click grant access shows connected', async ({ page }) => {
    await page.goto(PORTAL_URL);
    const btn = page.locator('#grantBtn');
    await btn.click();
    const status = page.locator('#status.success');
    await expect(status).toBeVisible({ timeout: 10000 });
    await expect(status).toContainText(/Connected/i);
  });

  test('captive detection URIs return portal', async ({ page }) => {
    const uris = ['/generate_204', '/hotspot-detect.html', '/canonical.html', '/success.txt'];
    for (const uri of uris) {
      const resp = await page.goto(`${PORTAL_URL}${uri}`);
      expect(resp.status()).toBe(200);
      const body = await resp.text();
      expect(body).toContain('TollGate');
    }
  });

  test('/api/status returns JSON with price', async ({ page }) => {
    const resp = await page.goto(`${PORTAL_URL}/api/status`);
    expect(resp.status()).toBe(200);
    const data = await resp.json();
    expect(data).toHaveProperty('connected');
    expect(data).toHaveProperty('price');
    expect(typeof data.price).toBe('number');
  });

  test('/whoami returns mac address', async ({ page }) => {
    const resp = await page.goto(`${PORTAL_URL}/whoami`);
    expect(resp.status()).toBe(200);
    const text = await resp.text();
    expect(text).toMatch(/^mac=/);
  });

  test('/usage returns -1/-1 before auth', async ({ page }) => {
    const resp = await page.goto(`${PORTAL_URL}/usage`);
    expect(resp.status()).toBe(200);
    const text = await resp.text();
    expect(text).toBe('-1/-1');
  });

  test('/reset_authentication works', async ({ page }) => {
    const resp = await page.goto(`${PORTAL_URL}/reset_authentication`);
    expect(resp.status()).toBe(200);
    const data = await resp.json();
    expect(data.status).toBe('reset');
  });
});
