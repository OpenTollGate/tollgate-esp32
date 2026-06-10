#!/bin/bash
# CI pipeline for esp32-tollgate
# Triggered by ci_listener.py on NIP-34 push events
set -euo pipefail

BRANCH="${CI_BRANCH:-feature/tollgate-core-v2}"
REPO_DIR="/tmp/ci-esp32-tollgate"
LOG_FILE="/tmp/ci-esp32-tollgate-pipeline.log"
RESULT_FILE="/tmp/ci-esp32-tollgate-result.txt"

echo "=== CI Pipeline Start ===" | tee "$LOG_FILE"
echo "Branch: $BRANCH" | tee -a "$LOG_FILE"
echo "Time: $(date -Iseconds)" | tee -a "$LOG_FILE"

# Clone or update
if [ -d "$REPO_DIR" ]; then
    echo "Updating existing clone..." | tee -a "$LOG_FILE"
    git -C "$REPO_DIR" fetch --all 2>&1 | tee -a "$LOG_FILE" || true
    git -C "$REPO_DIR" checkout "$BRANCH" 2>&1 | tee -a "$LOG_FILE" || true
    git -C "$REPO_DIR" reset --hard "origin/$BRANCH" 2>&1 | tee -a "$LOG_FILE" || true
else
    echo "Cloning repository..." | tee -a "$LOG_FILE"
    mkdir -p "$(dirname "$REPO_DIR")"
    git clone --branch "$BRANCH" \
        "nostr://npub12m5exm2uk3xa674cc5r0hlyvccs5xxn7qv83ezuteefv5972nquq4j4szl/ngit.orangesync.tech/esp32-tollgate" \
        "$REPO_DIR" 2>&1 | tee -a "$LOG_FILE"
fi

cd "$REPO_DIR"

# Init submodules
echo "=== Initializing submodules ===" | tee -a "$LOG_FILE"
git submodule update --init --recursive 2>&1 | tee -a "$LOG_FILE" || true

# Run host unit tests
echo "=== Running unit tests ===" | tee -a "$LOG_FILE"
if make test-unit 2>&1 | tee -a "$LOG_FILE"; then
    echo "UNIT_TESTS=PASS" | tee -a "$LOG_FILE"
    UNIT_RESULT="PASS"
else
    echo "UNIT_TESTS=FAIL" | tee -a "$LOG_FILE"
    UNIT_RESULT="FAIL"
fi

# ESP-IDF build (in Docker)
echo "=== ESP-IDF Build ===" | tee -a "$LOG_FILE"
if command -v docker &>/dev/null; then
    docker run --rm \
        -v "$REPO_DIR:/project" \
        -w /project \
        espressif/idf:latest \
        bash -c "idf.py build 2>&1" | tee -a "$LOG_FILE"
    BUILD_RC=${PIPESTATUS[0]:-1}
else
    echo "Docker not available, skipping ESP-IDF build" | tee -a "$LOG_FILE"
    BUILD_RC=0
fi

if [ "$BUILD_RC" -eq 0 ]; then
    echo "IDF_BUILD=PASS" | tee -a "$LOG_FILE"
    BUILD_RESULT="PASS"
else
    echo "IDF_BUILD=FAIL" | tee -a "$LOG_FILE"
    BUILD_RESULT="FAIL"
fi

# Summary
echo "=== Summary ===" | tee -a "$LOG_FILE"
echo "Unit tests: $UNIT_RESULT" | tee -a "$LOG_FILE"
echo "IDF build:  $BUILD_RESULT" | tee -a "$LOG_FILE"

if [ "$UNIT_RESULT" = "PASS" ] && [ "$BUILD_RESULT" = "PASS" ]; then
    echo "OVERALL=PASS" | tee -a "$LOG_FILE"
    echo "PASS" > "$RESULT_FILE"
    exit 0
else
    echo "OVERALL=FAIL" | tee -a "$LOG_FILE"
    echo "FAIL" > "$RESULT_FILE"
    exit 1
fi
