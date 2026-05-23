#!/usr/bin/env python3
"""Nostr relay listener for CI/CD triggers.

Subscribes to wss://ngit.orangesync.tech for NIP-34 git state events (kind 30617).
When a push event is detected for esp32-tollgate, triggers the CI pipeline.
"""

import json
import asyncio
import hashlib
import subprocess
import logging
import os
import sys
import time
from datetime import datetime, timezone

try:
    import websockets
except ImportError:
    print("websockets not installed. Run: pip install websockets")
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("ci-listener")

RELAY_URL = os.environ.get("CI_RELAY_URL", "wss://ngit.orangesync.tech")
REPO_ID = os.environ.get("CI_REPO_ID", "npub12m5exm2uk3xa674cc5r0hlyvccs5xxn7qv83ezuteefv5972nquq4j4szl/esp32-tollgate")
BRANCH = os.environ.get("CI_BRANCH", "feature/tollgate-core-v2")
PIPELINE_SCRIPT = os.environ.get("CI_PIPELINE_SCRIPT", os.path.join(os.path.dirname(__file__), "run_pipeline.sh"))
RECONNECT_DELAY = 5
POLL_TIMEOUT = 300


def generate_sub_id():
    h = hashlib.sha256(f"ci-listener-{time.time()}".encode()).hexdigest()
    return f"ci-{h[:16]}"


async def run_pipeline(event_data):
    log.info("Triggering CI pipeline for branch=%s", BRANCH)
    env = os.environ.copy()
    env["CI_EVENT_JSON"] = json.dumps(event_data)
    env["CI_BRANCH"] = BRANCH
    try:
        proc = subprocess.run(
            ["bash", PIPELINE_SCRIPT],
            env=env,
            capture_output=True,
            text=True,
            timeout=600,
        )
        log.info("Pipeline exit=%d", proc.returncode)
        if proc.stdout:
            for line in proc.stdout.strip().split("\n"):
                log.info("  pipeline: %s", line)
        if proc.returncode != 0 and proc.stderr:
            for line in proc.stderr.strip().split("\n"):
                log.error("  pipeline: %s", line)
        return proc.returncode
    except subprocess.TimeoutExpired:
        log.error("Pipeline timed out after 600s")
        return 1
    except Exception as e:
        log.error("Pipeline failed: %s", e)
        return 1


async def listen():
    sub_id = generate_sub_id()
    filters = {
        "kinds": [30617],
        "#d": [REPO_ID],
        "limit": [1],
    }

    while True:
        try:
            log.info("Connecting to %s", RELAY_URL)
            async with websockets.connect(RELAY_URL, ping_interval=30) as ws:
                req = json.dumps(["REQ", sub_id, filters])
                await ws.send(req)
                log.info("Subscribed to kind 30617 for %s", REPO_ID)

                last_event_id = None

                while True:
                    try:
                        msg = await asyncio.wait_for(ws.recv(), timeout=POLL_TIMEOUT)
                    except asyncio.TimeoutError:
                        await ws.send(json.dumps(["PING", sub_id]))
                        continue

                    data = json.loads(msg)

                    if data[0] == "EVENT":
                        event = data[2]
                        eid = event.get("id", "")
                        if eid == last_event_id:
                            continue
                        last_event_id = eid

                        tags = {k: [] for k in ["d", "branch", "commit", "clone"]}
                        for t in event.get("tags", []):
                            if len(t) >= 2 and t[0] in tags:
                                tags[t[0]].append(t[1])

                        event_branch = tags.get("branch", [None])[0]
                        if event_branch and event_branch != BRANCH:
                            log.info("Ignoring branch=%s (want %s)", event_branch, BRANCH)
                            continue

                        log.info("New push: commit=%s branch=%s",
                                 tags.get("commit", ["?"])[0],
                                 event_branch or BRANCH)
                        rc = await run_pipeline(event)
                        log.info("Pipeline result: %s", "PASS" if rc == 0 else "FAIL")

                    elif data[0] == "EOSE":
                        log.info("End of stored events, listening for new ones...")

                    elif data[0] == "OK":
                        pass

                    elif data[0] == "NOTICE":
                        log.warning("Relay notice: %s", data[1] if len(data) > 1 else data)

        except websockets.ConnectionClosed as e:
            log.warning("Connection closed: %s, reconnecting in %ds", e, RECONNECT_DELAY)
        except Exception as e:
            log.error("Error: %s, reconnecting in %ds", e, RECONNECT_DELAY)

        await asyncio.sleep(RECONNECT_DELAY)


def main():
    log.info("CI Listener starting: relay=%s repo=%s branch=%s", RELAY_URL, REPO_ID, BRANCH)
    asyncio.run(listen())


if __name__ == "__main__":
    main()
