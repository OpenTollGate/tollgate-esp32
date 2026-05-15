# TollGate ESP32 — Progress Checklist

## Phase 0: Bootstrap
- [x] Create project directory and git repo
- [x] Create .env, .env.example, .gitignore
- [x] Persist PLAN.md and CHECKLIST.md
- [ ] Create ESP-IDF project skeleton
- [ ] Create Makefile with detect targets
- [ ] Run `make detect-all` — identify ESP32 boards

## Phase 1: Captive Portal + Firewall
- [ ] Implement tollgate_main.c (WiFi AP+STA, event loop)
- [ ] Implement config.c/h (JSON config loading)
- [ ] Implement dns_server.c/h (DNS hijack/forward)
- [ ] Implement captive_portal.c/h (HTTP :80, portal HTML)
- [ ] Implement firewall.c/h (NAPT, per-IP auth)
- [ ] Set up test infrastructure (Playwright, helpers)
- [ ] Test 1: Boot and AP appears
- [ ] Test 2: DHCP lease
- [ ] Test 3: Captive portal serves HTML
- [ ] Test 4: Captive detection URIs work
- [ ] Test 5: DNS hijack before auth
- [ ] Test 6: No internet before auth
- [ ] Test 7: /whoami returns MAC
- [ ] Test 8: /usage returns no session
- [ ] Test 9: Grant access via API
- [ ] Test 10: DNS forward after auth
- [ ] Test 11: Internet after auth
- [ ] Test 12: HTTP browsing works
- [ ] Test 13: Reset auth
- [ ] Test 14: Internet blocked after reset

## Phase 2: E-Cash Payments (Simple Melt)
- [ ] Implement payment.c/h (Cashu token parse + melt)
- [ ] Implement session.c/h (time-based metering)
- [ ] Implement tollgate_api.c/h (:2121 endpoints)
- [ ] Update captive portal HTML with payment form
- [ ] Test 15: Advertisement valid
- [ ] Test 16: Valid payment
- [ ] Test 17: Usage tracking
- [ ] Test 18: Internet after payment
- [ ] Test 19: Invalid token rejected
- [ ] Test 20: Spent token rejected
- [ ] Test 21: Wrong mint rejected
- [ ] Test 22: Session expiry
- [ ] Test 23: Session renewal
- [ ] Test 24: Portal payment form
- [ ] Test 25: Two clients pay independently
- [ ] Test 26: Client isolation
- [ ] Test 27: Full e2e browser flow

## Phase 3: nucula Wallet + Reseller
- [ ] Extract nucula wallet into components/cashu_wallet/
- [ ] Replace simple melt with Wallet::receive()
- [ ] Implement payout.c/h (background melt-to-LN)
- [ ] Implement upstream_client.c/h (reseller mode)
- [ ] Test 28-38: All Phase 3 tests
