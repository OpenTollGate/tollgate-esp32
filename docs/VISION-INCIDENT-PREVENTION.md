# Vision/Proxy Incident Prevention Plan

## Incident Summary (2026-07-05)

**Symptom:** Vision analysis (image recognition) silently failed when user sent hardware photos.

**Root Causes (3 cascading failures):**

1. **PPQ API key revoked** — Hermes vision config pointed to `api.ppq.ai` with model `gemini-3-flash-preview`. The PPQ API key had been revoked/expired. Any vision request → 401.

2. **z.ai fallback model not in plan** — Even after switching to z.ai, the configured model was `glm-5v-turbo`. Our z.ai subscription plan does NOT include this model (error 1311: "Your current subscription plan does not yet include access to GLM-5V-Turbo"). Only `glm-4.6v` is available for vision.

3. **No automated detection** — Nothing was checking whether the vision endpoint actually worked. The failure was silent until the user tried to use it and got garbled output from Google Lens as a fallback.

## Fix Applied

```
hermes config set auxiliary.vision.provider zai
hermes config set auxiliary.vision.model glm-4.6v
hermes config set auxiliary.vision.base_url http://127.0.0.1:9099
```

Vision confirmed working through z.ai proxy (auto-rotates between both API keys).

## Prevention Measures

### 1. Automated Health Check (Active)

**Script:** `~/.hermes/profiles/manager/scripts/vision_health_check.py`
**Cron:** `vision-health-check` (job `211f0050de4e`, every 30min, `no_agent=True`)
**Delivery:** `origin` (alerts to active chat when broken, SILENT when healthy)

**What it checks:**
- Reads vision config from Hermes config.yaml
- Sends a minimal vision request (1x1 red pixel PNG) to the configured endpoint
- Validates HTTP response and content
- Detects: connection failures, auth errors (401/403), model-not-in-plan (error 1311), rate limiting (429), empty responses
- Pre-checks z.ai quota state — skips if throttled (proxy auto-rotates anyway)

**Failure modes detected:**

| Failure | Detection | Alert |
|---------|-----------|-------|
| Proxy down | Connection error | "VISION DOWN: Cannot reach proxy" |
| API key revoked | HTTP 401/403 | "VISION AUTH FAILED" |
| Model not in plan | Error 1311 in response | "VISION MODEL NOT IN PLAN" + fix command |
| Rate limited | HTTP 429 | "VISION RATE LIMITED" |
| Empty response | 200 but no content | "VISION WARNING: empty content" |

### 2. Config Single Source of Truth

Vision config lives in `~/.hermes/profiles/manager/config.yaml`:
```yaml
auxiliary:
  vision:
    provider: zai
    model: glm-4.6v           # CONFIRMED working, NOT glm-5v-turbo
    base_url: http://127.0.0.1:9099
    api_key: ''               # empty = proxy handles auth
```

### 3. Known Working Models

| Provider | Model | Status |
|----------|-------|--------|
| z.ai | `glm-4.6v` | ✅ Working |
| z.ai | `glm-5v-turbo` | ❌ Not in subscription plan |
| PPQ | `gemini-3-flash-preview` | ❌ API key revoked |

### 4. Quota Auto-Rotation (Existing)

The z.ai proxy at `127.0.0.1:9099` already auto-rotates between two API keys:
- **Our key:** Has weekly quota limit (100% used as of 2026-07-05, resets July 10)
- **Friend's key:** No weekly limit (5hr: 39%, monthly: 14%)

When our key is exhausted, the proxy transparently routes to the friend's key. No manual intervention needed.

## Incident Response Playbook

If `vision-health-check` fires an alert:

1. **Check the alert message** — it includes the specific failure type and suggested fix
2. **For "MODEL NOT IN PLAN":** Run `hermes config set auxiliary.vision.model glm-4.6v`
3. **For "AUTH FAILED":** Check if API key needs renewal at the provider's dashboard
4. **For "VISION DOWN":** Restart z.ai proxy: check `zai-proxy-management` skill
5. **Verify fix:** Run `python3 ~/.hermes/profiles/manager/scripts/vision_health_check.py` manually

## Git Tracking

- Script: committed to `c03rad0r/hermes-bot` (state replication)
- This doc: committed to `OpenTollGate/tollgate-esp32` (project docs)
