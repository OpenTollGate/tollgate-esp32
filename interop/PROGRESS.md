# PROGRESS.md — Interop Test Checklist

## Setup

- [ ] Create `interop/routers.env` from `routers.env.example`
- [ ] Verify SSH access to OpenWRT: `ssh root@10.47.41.1 echo ok`
- [ ] Verify WiFi connection to ESP32: `ping -c 2 10.192.45.1`
- [ ] Build `mint-token` binary: `cd physical-router-test-automation/scripts/mint-token && go build -o /tmp/mint-token .`
- [ ] Install `cashu` CLI: `pip install cashu`

## Mint Alignment

- [ ] Add `testnut.cashu.space` to OpenWRT's `accepted_mints`
- [ ] Add `nofee.testnut.cashu.space` to ESP32's config
- [ ] Verify both mints accepted on OpenWRT
- [ ] Verify both mints accepted on ESP32

## Wallet Funding

- [ ] Fund ESP32 wallet via `cashu send --legacy` (V3 token)
- [ ] Fund OpenWRT wallet via `mint-token` (V4 token)
- [ ] Verify ESP32 balance > 0
- [ ] Verify OpenWRT balance > 0

## Scenario 1: Laptop → ESP32

- [ ] `make interop-laptop-esp32` — mint V3 token, POST to ESP32, verify internet
- [ ] Token accepted (kind=1022)
- [ ] Internet works after payment
- [ ] Spent token rejected (kind=21023)

## Scenario 2: Laptop → OpenWRT

- [ ] `make interop-laptop-openwrt` — mint V4 token, POST to OpenWRT, verify internet
- [ ] Token accepted (kind=1022)
- [ ] Internet works after payment
- [ ] Spent token rejected (kind=21023)

## Scenario 3: OpenWRT → ESP32 (Reseller)

- [ ] `make interop-openwrt-esp32` — OpenWRT connects to ESP32 AP, auto-pays
- [ ] OpenWRT STA connects to `TollGate-C0E9CA`
- [ ] OpenWRT daemon detects TollGate upstream
- [ ] Auto-payment succeeds (ESP32 session created)
- [ ] OpenWRT has internet through ESP32
- [ ] ESP32 wallet balance increased
- [ ] Cleanup: restore OpenWRT upstream

## Scenario 5: ESP32 ↔ ESP32

- [ ] Flash Board B with different nsec
- [ ] Configure Board B's config.json
- [ ] Fund Board B wallet
- [ ] `make interop-esp32-esp32` — cross-board payment
- [ ] Cleanup: restore both boards

## Cleanup

- [ ] Restore OpenWRT production config
- [ ] Restore ESP32 original config (if changed)
- [ ] Verify both devices back to normal operation

## Infrastructure

- [x] `interop/INTEROP_PLAN.md` written
- [ ] `interop/AGENTS.md` written
- [ ] `interop/routers.env.example` written
- [ ] `interop/Makefile` written
- [ ] `interop-status` target tested against real hardware
- [ ] Committed and pushed
