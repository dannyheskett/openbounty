#!/usr/bin/env python3
"""Upload the Glory of Rome app (.apk or .ipa) to AWS Device Farm and run a
Fuzz test on a real device, reporting pass/fail. Used by the devicefarm CI
workflow.

This is the only way this repository can answer "does it work on real
hardware". The GitHub runners have no phone, and the Android emulator's
software GL gives raylib no usable EGL configuration, so an emulator run
cannot tell a broken app from a broken emulator. The built-in Fuzz test
(random taps) is the right shape here: the game draws its own UI rather than
using widgets a scripted test could find, so it is the launch-and-survive
check that matters, on glass. The platform is inferred from the app file
extension; an unsigned iOS .ipa works because Device Farm re-signs on upload.

Config via env (no ARNs are hard-coded, so this file is safe to commit):
  DEVICEFARM_PROJECT_ARN     required  the Device Farm project ARN
  APP_PATH                   required  path to the .apk or .ipa to test
  DEVICEFARM_UPLOAD_ONLY     optional  "1" to stop after upload (free; no devices)
  DEVICEFARM_MAX_DEVICES     optional  device count for the run (default 1)
  DEVICEFARM_DEVICE_MODEL    optional  MODEL substring filter, e.g. "Galaxy S23"
  AWS_REGION                 optional  defaults to us-west-2
"""
import os
import shutil
import sys
import time
import urllib.request

import boto3

REGION = os.environ.get("AWS_REGION", "us-west-2")
PROJECT = os.environ["DEVICEFARM_PROJECT_ARN"]
APP_PATH = os.environ["APP_PATH"]
UPLOAD_ONLY = os.environ.get("DEVICEFARM_UPLOAD_ONLY") == "1"
MAX_DEVICES = int(os.environ.get("DEVICEFARM_MAX_DEVICES", "1"))
DEVICE_MODEL = os.environ.get("DEVICEFARM_DEVICE_MODEL", "").strip()

if APP_PATH.endswith(".apk"):
    PLATFORM, UPLOAD_TYPE = "ANDROID", "ANDROID_APP"
elif APP_PATH.endswith(".ipa"):
    PLATFORM, UPLOAD_TYPE = "IOS", "IOS_APP"
else:
    sys.exit(f"APP_PATH must be a .apk or .ipa, got: {APP_PATH}")

df = boto3.client("devicefarm", region_name=REGION)


def poll(fn, done, desc, timeout=1800, interval=10):
    start = time.time()
    while True:
        obj = fn()
        status = obj["status"]
        if done(obj):
            return obj
        if time.time() - start > timeout:
            sys.exit(f"timed out waiting for {desc} (last status {status})")
        print(f"  {desc}: {status} ...", flush=True)
        time.sleep(interval)


def paginate(fn, key, **kw):
    """Yield every item across all pages. Device Farm's list_* APIs page at 50.

    max_devices is user-settable and yields one job per device, so an
    unpaginated walk silently sees only the first page while the code around it
    speaks as though it had seen everything.
    """
    token = None
    while True:
        page = fn(**kw, nextToken=token) if token else fn(**kw)
        yield from page[key]
        token = page.get("nextToken")
        if not token:
            return


def download_media_artifacts(run_arn, out_dir):
    """Download the run's screenshots and video (skipping logs) so CI can upload
    them as a workflow artifact for visual inspection."""
    os.makedirs(out_dir, exist_ok=True)
    n = 0
    for job in paginate(df.list_jobs, "jobs", arn=run_arn):
        for suite in paginate(df.list_suites, "suites", arn=job["arn"]):
            sname = suite["name"].replace(" ", "_")
            for test in paginate(df.list_tests, "tests", arn=suite["arn"]):
                for atype in ("SCREENSHOT", "FILE"):
                    for a in paginate(df.list_artifacts, "artifacts", arn=test["arn"], type=atype):
                        ext = (a.get("extension") or "").lower()
                        if ext not in ("mp4", "png", "jpg", "jpeg"):
                            continue
                        name = a["name"].replace(" ", "_")
                        fn = os.path.join(out_dir, f"{sname}-{name}.{ext}")
                        # urlretrieve has no timeout parameter, so a stalled
                        # transfer hung until the job timeout.
                        with urllib.request.urlopen(a["url"], timeout=120) as r, \
                                open(fn, "wb") as out:
                            shutil.copyfileobj(r, out)
                        n += 1
    print(f"downloaded {n} media artifact(s) to {out_dir}", flush=True)


def main():
    # 1) Register an upload slot and PUT the app to the presigned URL.
    up = df.create_upload(
        projectArn=PROJECT, name=os.path.basename(APP_PATH), type=UPLOAD_TYPE
    )["upload"]
    with open(APP_PATH, "rb") as f:
        data = f.read()
    req = urllib.request.Request(
        up["url"], data=data, method="PUT",
        headers={"Content-Type": "application/octet-stream"},
    )
    urllib.request.urlopen(req, timeout=300).read()
    print(f"uploaded {len(data)} bytes ({APP_PATH})", flush=True)

    # 2) Wait for Device Farm to validate the app.
    up = poll(
        lambda: df.get_upload(arn=up["arn"])["upload"],
        lambda o: o["status"] in ("SUCCEEDED", "FAILED"),
        "upload",
        timeout=300,
    )
    if up["status"] != "SUCCEEDED":
        sys.exit(f"upload failed: {up.get('metadata')}")
    print("upload validated: SUCCEEDED", flush=True)

    if UPLOAD_ONLY:
        print("DEVICEFARM_UPLOAD_ONLY set — stopping before scheduling a run.")
        return

    # 3) Schedule a Fuzz run on real device(s) of the matching platform.
    filters = [
        {"attribute": "PLATFORM", "operator": "EQUALS", "values": [PLATFORM]},
        {"attribute": "AVAILABILITY", "operator": "EQUALS", "values": ["HIGHLY_AVAILABLE"]},
    ]
    if DEVICE_MODEL:
        # e.g. "Galaxy S23" / "Pixel 8" — pick a particular phone instead of
        # whatever happens to be free.
        filters.append({"attribute": "MODEL", "operator": "CONTAINS", "values": [DEVICE_MODEL]})

    test_spec = {"type": "BUILTIN_FUZZ"}

    run = df.schedule_run(
        projectArn=PROJECT,
        appArn=up["arn"],
        name=f"Glory of Rome CI fuzz ({PLATFORM})",
        test=test_spec,
        deviceSelectionConfiguration={
            "filters": filters,
            "maxDevices": MAX_DEVICES,
        },
    )["run"]
    print(f"scheduled run: {run['arn']}", flush=True)

    # 4) Wait for completion and report.
    run = poll(
        lambda: df.get_run(arn=run["arn"])["run"],
        lambda o: o["status"] == "COMPLETED",
        "run",
    )
    c = run.get("counters", {})
    print(f"run result: {run['result']}  counters={c}", flush=True)

    art_dir = os.environ.get("DEVICEFARM_ARTIFACT_DIR")
    if art_dir:
        download_media_artifacts(run["arn"], art_dir)

    if run["result"] != "PASSED":
        sys.exit(f"Device Farm run did not pass: {run['result']}")


if __name__ == "__main__":
    main()
