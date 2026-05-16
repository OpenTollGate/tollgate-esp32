import { test, expect } from '@playwright/test';

const PORTAL_IP = process.env.TOLLGATE_IP || '192.168.4.1';
const PORTAL_URL = `http://${PORTAL_IP}`;
const API_URL = `http://${PORTAL_IP}:2121`;

test.describe('Captive Portal - Phase 2', () => {

  test('portal page loads with TollGate branding', async ({ page }) => {
    await page.goto(PORTAL_URL);
    await expect(page.locator('h1')).toHaveText('TollGate');
    await expect(page.locator('.subtitle')).toContainText('internet access');
  });

  test('portal shows price from API', async ({ page }) => {
    await page.goto(PORTAL_URL);
    const priceEl = page.locator('.price-amount');
    await expect(priceEl).not.toBeEmpty({ timeout: 5000 });
  });

  test('portal has Cashu token input', async ({ page }) => {
    await page.goto(PORTAL_URL);
    const textarea = page.locator('#tokenInput');
    await expect(textarea).toBeVisible();
    await expect(textarea).toHaveAttribute('placeholder', /cashuA/);
  });

  test('portal has Pay & Connect button', async ({ page }) => {
    await page.goto(PORTAL_URL);
    const btn = page.locator('#payBtn');
    await expect(btn).toBeVisible();
    await expect(btn).toHaveText(/Pay/);
  });

  test('captive detection URIs return portal HTML (200)', async ({ request }) => {
    const uris = ['/generate_204', '/hotspot-detect.html', '/canonical.html', '/success.txt'];
    for (const uri of uris) {
      const resp = await request.fetch(`${PORTAL_URL}${uri}`);
      expect(resp.status()).toBe(200);
      const body = await resp.text();
      expect(body).toContain('TollGate');
    }
  });

  test('catch-all URIs redirect to portal page', async ({ page }) => {
    await page.goto(`${PORTAL_URL}/some-random-page`);
    await expect(page.locator('h1')).toHaveText('TollGate');
  });

  test('/whoami returns ip and mac', async ({ page }) => {
    const resp = await page.goto(`${API_URL}/whoami`);
    expect(resp.status()).toBe(200);
    const text = await resp.text();
    expect(text).toMatch(/ip=\d+\.\d+\.\d+\.\d+/);
    expect(text).toMatch(/mac=[0-9a-f]{2}:/);
  });

  test('/usage returns -1/-1 before payment', async ({ page }) => {
    const resp = await page.goto(`${API_URL}/usage`);
    expect(resp.status()).toBe(200);
    const text = await resp.text();
    expect(text).toBe('-1/-1');
  });

  test('API advertisement has correct structure', async ({ page }) => {
    const resp = await page.goto(API_URL);
    expect(resp.status()).toBe(200);
    const data = await resp.json();
    expect(data.kind).toBe(10021);
    expect(data.tags).toBeDefined();
    expect(data.tags.some(t => t[0] === 'price_per_step')).toBe(true);
    expect(data.tags.some(t => t[0] === 'step_size')).toBe(true);
  });

  test('invalid token returns error', async ({ request }) => {
    const resp = await request.fetch(API_URL, {
      method: 'POST',
      data: 'garbage_not_a_token'
    });
    expect(resp.status()).toBe(400);
    const data = await resp.json();
    expect(data.kind).toBe(21023);
  });
});
