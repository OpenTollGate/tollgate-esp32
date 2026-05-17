import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: '.',
  testMatch: '*.spec.mjs',
  timeout: 120000,
  retries: 0,
  use: {
    headless: true,
    viewport: { width: 1280, height: 900 },
    screenshot: 'on',
    video: 'retain-on-failure',
    trace: 'on-first-retry',
  },
  reporter: [['list'], ['html', { open: 'never' }]],
  outputDir: 'test-results',
  workers: 1,
});
