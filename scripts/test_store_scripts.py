#!/usr/bin/env python3
"""Tests for the store scripts' failure handling: the bounded retry
(net_retry.py), asc_release.py replacing a pending App Review, re-uploading a
stalled screenshot, and retrying Apple's "not ready yet" submit refusal. No network:
the transport and App Store Connect are faked. Run by CI (ci.yml, linux).

Exit status is non-zero on any failure.
"""
import io
import sys
import tempfile
import urllib.error
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import net_retry  # noqa: E402

failures = 0


def check(cond, what):
    global failures
    if cond:
        print(f"  ok   {what}")
    else:
        print(f"  FAIL {what}")
        failures += 1


def expect_exit(fn, what, contains=None):
    try:
        fn()
    except SystemExit as e:
        msg = str(e.code)
        check(contains is None or contains in msg, f"{what} (exit: {msg.splitlines()[0][:90]})")
        return msg
    check(False, f"{what} (did not exit)")
    return None


# --- net_retry -----------------------------------------------------------------
class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


def http_error(code):
    return urllib.error.HTTPError("https://x", code, "err", {}, io.BytesIO(b'{"e":1}'))


def scripted(outcomes):
    """A fake urlopen returning/raising the given outcomes in order."""
    calls = {"n": 0}

    def fake(req, timeout=None):
        o = outcomes[calls["n"]]
        calls["n"] += 1
        if isinstance(o, BaseException):
            raise o
        return FakeResponse(o)
    return fake, calls


def run_retry(outcomes):
    slept = []
    net_retry.sleep = slept.append
    net_retry.urlopen, calls = scripted(outcomes)
    try:
        body = net_retry.request(object(), "GET /x")
        return body, None, calls["n"], slept
    except Exception as e:  # noqa: BLE001 - the test inspects the type
        return None, e, calls["n"], slept


def test_retry():
    print("net_retry")
    body, err, n, slept = run_retry([http_error(503), http_error(503), b"ok"])
    check(body == b"ok" and n == 3 and slept == [10, 30], "503, 503, 200 -> succeeds after 2 retries")

    body, err, n, slept = run_retry([http_error(503)] * 4)
    check(isinstance(err, net_retry.HTTPFailure) and n == 4 and slept == [10, 30, 90],
          "503 x4 -> fails after 3 retries")
    check(err is not None and "still failing after 3 retries" in str(err),
          "the final failure says it was retried")

    for code in (409, 404, 400, 401):
        body, err, n, slept = run_retry([http_error(code), b"never"])
        check(isinstance(err, net_retry.HTTPFailure) and err.code == code and n == 1 and not slept,
              f"{code} -> fails immediately, no retry")

    body, err, n, slept = run_retry([http_error(429), b"ok"])
    check(body == b"ok" and n == 2, "429 -> retried")

    body, err, n, slept = run_retry([urllib.error.URLError("reset"), TimeoutError("t"), b"ok"])
    check(body == b"ok" and n == 3, "network error, timeout -> retried, then succeeds")

    body, err, n, slept = run_retry([urllib.error.URLError("down")] * 4)
    check(isinstance(err, net_retry.NetworkFailure) and n == 4, "network error x4 -> fails")


# --- asc_release: replacing a pending review ------------------------------------
def load_asc():
    import asc_release
    asc_release.sleep = lambda s: None
    return asc_release


class FakeASC:
    """App Store Connect as a small state machine. `after_cancel` is the state
    the version moves to once its submission is cancelled (None = never)."""

    def __init__(self, asc_release, state, after_cancel="DEVELOPER_REJECTED",
                 refuse_cancel=False, publishes=True, has_submission=True):
        self.m = asc_release
        self.dry_run = False
        self.state = state
        self.after_cancel = after_cancel
        self.refuse_cancel = refuse_cancel
        self.publishes = publishes
        self.has_submission = has_submission
        self.cancelled = False
        self.deleted_items = []
        self.polls = 0

    # The bits of ASC that replace_pending uses.
    def versions(self, app):
        self.polls += 1
        if self.state in self.m.PUBLISHING and self.publishes and self.polls > 2:
            self.state = "READY_FOR_SALE"
        return [{"id": "v1", "attributes": {"versionString": "1.0.7", "appStoreState": self.state}}]

    def version_state(self, app, vid):
        return self.versions(app)[0]["attributes"]["appStoreState"]

    def in_flight_version(self, app):
        v = self.versions(app)[0]
        return v if v["attributes"]["appStoreState"] in self.m.IN_FLIGHT else None

    def call(self, method, path, body=None, raise_errors=False):
        if method == "GET" and "reviewSubmissions" in path and "READY_FOR_REVIEW" in path:
            return {"data": [{"id": "draft"}],
                    "included": [{"type": "reviewSubmissionItems", "id": "item1"}]}
        if method == "GET" and "reviewSubmissions" in path:
            return {"data": [{"id": "sub1"}] if self.has_submission else []}
        if method == "PATCH" and "reviewSubmissions/sub1" in path:
            if self.refuse_cancel:
                raise self.m.ASCError(409, method, path, '{"errors":[{"code":"STATE_ERROR"}]}')
            self.cancelled = body["data"]["attributes"].get("canceled") is True
            if self.after_cancel:
                self.state = self.after_cancel
            return {}
        if method == "DELETE" and "reviewSubmissionItems" in path:
            self.deleted_items.append(path.rsplit("/", 1)[1])
            self.state = "PREPARE_FOR_SUBMISSION"
            return {}
        raise AssertionError(f"unexpected call {method} {path}")


def test_replace():
    print("asc_release replace_pending")
    m = load_asc()

    fake = FakeASC(m, "READY_FOR_SALE")
    m.replace_pending(fake, "app")
    check(not fake.cancelled, "nothing pending -> nothing withdrawn")

    for state in ("WAITING_FOR_REVIEW", "IN_REVIEW"):
        fake = FakeASC(m, state)
        m.replace_pending(fake, "app")
        check(fake.cancelled and fake.state in m.EDITABLE,
              f"{state} -> submission cancelled, version editable again")

    fake = FakeASC(m, "READY_FOR_REVIEW")
    m.replace_pending(fake, "app")
    check(fake.deleted_items == ["item1"] and fake.state in m.EDITABLE,
          "READY_FOR_REVIEW draft -> item removed, version editable")

    fake = FakeASC(m, "WAITING_FOR_REVIEW", refuse_cancel=True)
    expect_exit(lambda: m.replace_pending(fake, "app"),
                "Apple refuses the withdrawal -> run fails, naming the version", "1.0.7")

    fake = FakeASC(m, "WAITING_FOR_REVIEW", has_submission=False)
    expect_exit(lambda: m.replace_pending(fake, "app"),
                "no open submission found -> run fails", "no open review")

    clock = {"t": 0.0}
    m.monotonic = lambda: clock.__setitem__("t", clock["t"] + 100) or clock["t"]
    fake = FakeASC(m, "WAITING_FOR_REVIEW", after_cancel=None)
    expect_exit(lambda: m.replace_pending(fake, "app"),
                "withdrawal never completes -> run fails on timeout", "still WAITING_FOR_REVIEW")

    clock["t"] = 0.0
    fake = FakeASC(m, "PENDING_APPLE_RELEASE")
    m.replace_pending(fake, "app")
    check(fake.state == "READY_FOR_SALE" and not fake.cancelled,
          "PENDING_APPLE_RELEASE -> waited until live, nothing withdrawn")

    clock["t"] = 0.0
    fake = FakeASC(m, "PROCESSING_FOR_APP_STORE", publishes=False)
    expect_exit(lambda: m.replace_pending(fake, "app"),
                "publishing never finishes -> run fails on timeout", "still PROCESSING_FOR_APP_STORE")

    fake = FakeASC(m, "PENDING_DEVELOPER_RELEASE")
    expect_exit(lambda: m.replace_pending(fake, "app"),
                "held for manual release -> run fails, naming the version", "1.0.7")


class FakeSubmitted:
    """App Store Connect right after a submission: READY_FOR_REVIEW for
    `settle_polls` reads, then `final`."""

    def __init__(self, settle_polls, final="WAITING_FOR_REVIEW", build="7"):
        self.dry_run = False
        self.reads = 0
        self.settle_polls = settle_polls
        self.final = final
        self.build = build

    def app_id(self):
        return "app"

    def versions(self, app):
        self.reads += 1
        state = "READY_FOR_REVIEW" if self.reads <= self.settle_polls else self.final
        return [{"id": "v1", "attributes": {"versionString": "1.0.7", "appStoreState": state}}]

    def version_state(self, app, vid):
        return self.versions(app)[0]["attributes"]["appStoreState"]

    def call(self, method, path, body=None, raise_errors=False):
        if path.endswith("/build"):
            return {"data": {"attributes": {"version": self.build}}}
        raise AssertionError(f"unexpected call {method} {path}")


def test_verify_submitted():
    print("asc_release verify-submitted")
    m = load_asc()

    class Args:
        build = 7
    m.monotonic = lambda: 0.0
    fake = FakeSubmitted(settle_polls=3)
    check(m.cmd_verify_submitted(fake, Args()) == 0,
          "READY_FOR_REVIEW right after submitting, then WAITING_FOR_REVIEW -> verified")

    clock = {"t": 0.0}
    m.monotonic = lambda: clock.__setitem__("t", clock["t"] + 60) or clock["t"]
    fake = FakeSubmitted(settle_polls=10**6)
    expect_exit(lambda: m.cmd_verify_submitted(fake, Args()),
                "stuck at READY_FOR_REVIEW -> fails at the deadline", "still READY_FOR_REVIEW")

    m.monotonic = lambda: 0.0
    fake = FakeSubmitted(settle_polls=0, final="REJECTED")
    expect_exit(lambda: m.cmd_verify_submitted(fake, Args()),
                "REJECTED -> fails at once", "not in review")

    fake = FakeSubmitted(settle_polls=0, build="6")
    expect_exit(lambda: m.cmd_verify_submitted(fake, Args()),
                "wrong build attached -> fails", "not 7")


class FakeShots:
    """A version's screenshots. `stuck` names files Apple never processes on
    their first `stuck[name]` uploads; every other upload finishes after one
    poll."""

    def __init__(self, asc_release, names, stuck=None, failed=None):
        self.m = asc_release
        self.dry_run = False
        self.stuck = dict(stuck or {})
        self.failed = set(failed or [])
        self.uploads = {n: 1 for n in names}
        self.shots = [{"id": f"{n}#1", "name": n, "polls": 0} for n in names]
        self.order_patches = []

    def state(self, shot):
        if shot["name"] in self.failed:
            return "FAILED"
        upload = int(shot["id"].split("#")[1])
        if upload <= self.stuck.get(shot["name"], 0):
            return "UPLOAD_COMPLETE"
        return "COMPLETE" if shot["polls"] > 1 else "UPLOAD_COMPLETE"

    def call(self, method, path, body=None, raise_errors=False):
        if path.endswith("/appStoreVersionLocalizations"):
            return {"data": [{"id": "loc"}]}
        if path.endswith("/appScreenshotSets"):
            return {"data": [{"id": "set", "attributes": {"screenshotDisplayType": "APP_IPHONE_67"}}]}
        if method == "GET" and "/appScreenshots" in path:
            for shot in self.shots:
                shot["polls"] += 1
            return {"data": [{"id": s["id"], "attributes": {
                "fileName": s["name"],
                "assetDeliveryState": {"state": self.state(s),
                                       "errors": [{"code": "BAD"}] if self.state(s) == "FAILED" else []}}}
                for s in self.shots]}
        if method == "DELETE" and path.startswith("/v1/appScreenshots/"):
            gone = path.rsplit("/", 1)[1]
            self.shots = [s for s in self.shots if s["id"] != gone]
            return {}
        if method == "POST" and path == "/v1/appScreenshots":
            name = body["data"]["attributes"]["fileName"]
            self.uploads[name] += 1
            shot = {"id": f"{name}#{self.uploads[name]}", "name": name, "polls": 0}
            self.shots.append(shot)
            return {"data": {"id": shot["id"], "attributes": {"uploadOperations": []}}}
        if method == "PATCH" and path.startswith("/v1/appScreenshots/"):
            return {}
        if method == "PATCH" and path.endswith("/relationships/appScreenshots"):
            want = [d["id"] for d in body["data"]]
            self.order_patches.append(want)
            self.shots.sort(key=lambda s: want.index(s["id"]))
            return {}
        raise AssertionError(f"unexpected call {method} {path}")


def test_screenshots(tmp):
    print("asc_release screenshot processing")
    m = load_asc()
    names = ["01.png", "02.png", "03.png"]
    for n in names:
        (tmp / n).write_bytes(b"png")
    m.SHOT_SETS = [("APP_IPHONE_67", tmp)]
    clock = {"t": 0.0}
    m.monotonic = lambda: clock["t"]
    m.sleep = lambda s: clock.__setitem__("t", clock["t"] + s)

    fake = FakeShots(m, names)
    m.wait_for_screenshots(fake, "v1", m.ASSET_WAIT_SECONDS)
    check(sum(fake.uploads.values()) == 3, "all process normally -> no re-upload")

    clock["t"] = 0.0
    fake = FakeShots(m, names, stuck={"02.png": 1})
    m.wait_for_screenshots(fake, "v1", m.ASSET_WAIT_SECONDS)
    check(fake.uploads["02.png"] == 2 and [s["name"] for s in fake.shots] == names,
          "one stuck at UPLOAD_COMPLETE -> re-uploaded once, set order kept")

    clock["t"] = 0.0
    fake = FakeShots(m, names, stuck={"03.png": 10})
    expect_exit(lambda: m.wait_for_screenshots(fake, "v1", m.ASSET_WAIT_SECONDS),
                "never processes -> fails after the re-uploads, naming it", "03.png")
    check(fake.uploads["03.png"] == 1 + m.ASSET_REUPLOADS,
          f"never processes -> exactly {m.ASSET_REUPLOADS} re-uploads")

    clock["t"] = 0.0
    fake = FakeShots(m, names, failed={"01.png"})
    expect_exit(lambda: m.wait_for_screenshots(fake, "v1", m.ASSET_WAIT_SECONDS),
                "FAILED -> fails at once", "failed to process")
    check(sum(fake.uploads.values()) == 3, "FAILED -> not re-uploaded")

    clock["t"] = 0.0
    m.SHOT_SETS = [("APP_IPHONE_67", tmp / "elsewhere")]
    fake = FakeShots(m, names, stuck={"02.png": 1})
    expect_exit(lambda: m.wait_for_screenshots(fake, "v1", m.ASSET_WAIT_SECONDS),
                "stuck with no repo file to re-upload -> fails", "no 02.png in the repo")


NOT_READY = ('{"errors":[{"code":"STATE_ERROR.ENTITY_STATE_INVALID","meta":{"associatedErrors":'
             '{"/v1/appStoreVersions/x":[{"code":"STATE_ERROR","detail":"Version is not ready '
             'to be submitted yet, please try again later."}]}}}]}')


class FakeSubmit:
    """The submit PATCH: refused with `refusal` for the first `refusals` calls."""

    def __init__(self, asc_release, refusals, refusal=NOT_READY):
        self.m = asc_release
        self.dry_run = False
        self.refusals = refusals
        self.refusal = refusal
        self.calls = 0

    def call(self, method, path, body=None, raise_errors=False):
        assert method == "PATCH" and body["data"]["attributes"] == {"submitted": True}
        self.calls += 1
        if self.calls <= self.refusals:
            raise self.m.ASCError(409, method, path, self.refusal)
        return {}


def test_submit():
    print("asc_release submit")
    m = load_asc()
    clock = {"t": 0.0}
    m.monotonic = lambda: clock["t"]
    m.sleep = lambda s: clock.__setitem__("t", clock["t"] + s)

    fake = FakeSubmit(m, refusals=3)
    m.submit_for_review(fake, "sub1")
    check(fake.calls == 4, "\"not ready yet\" x3 -> tried again, then submitted")

    clock["t"] = 0.0
    fake = FakeSubmit(m, refusals=10**6)
    expect_exit(lambda: m.submit_for_review(fake, "sub1"),
                "\"not ready yet\" past the deadline -> fails", "still says the version is not ready")

    clock["t"] = 0.0
    fake = FakeSubmit(m, refusals=1, refusal='{"errors":[{"code":"STATE_ERROR.ENTITY_STATE_INVALID",'
                                             '"detail":"Missing required screenshots."}]}')
    expect_exit(lambda: m.submit_for_review(fake, "sub1"),
                "any other 409 -> fails at once", "Missing required screenshots")
    check(fake.calls == 1, "any other 409 -> not retried")


def main():
    test_retry()
    test_replace()
    test_verify_submitted()
    with tempfile.TemporaryDirectory() as tmp:
        test_screenshots(Path(tmp))
    test_submit()
    if failures:
        print(f"FAILED: {failures} check(s)")
        return 1
    print("OK: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
