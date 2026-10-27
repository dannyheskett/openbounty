"""HTTP with a bounded retry for temporary errors, shared by the store scripts.

A temporary error -- HTTP 429, 500, 502, 503, 504, or a network failure or
timeout -- is retried up to len(DELAYS) times, waiting DELAYS[i] seconds before
retry i+1. Anything else (every other 4xx, and a temporary error that is still
failing after the last retry) raises, so the caller fails the run. Each retry is
printed, so a run that needed one says so.
"""
import http.client
import socket
import time
import urllib.error
import urllib.request

DELAYS = (10, 30, 90)
TRANSIENT_HTTP = {429, 500, 502, 503, 504}
TIMEOUT_SECONDS = 300

# Replaced in the tests so they don't wait.
sleep = time.sleep
urlopen = urllib.request.urlopen


class HTTPFailure(Exception):
    """A request that failed for good: a non-temporary HTTP error, or a
    temporary one that outlasted every retry."""

    def __init__(self, code, text, what):
        super().__init__(f"HTTP {code} on {what}\n{text[:1500]}")
        self.code = code
        self.text = text


class NetworkFailure(Exception):
    """A network failure or timeout that outlasted every retry."""


def request(req, what):
    """Send `req` and return the response body (bytes), retrying temporary
    errors. `what` names the call in messages, e.g. "POST /v1/builds"."""
    attempts = len(DELAYS) + 1
    for attempt in range(attempts):
        last = attempt == attempts - 1
        try:
            with urlopen(req, timeout=TIMEOUT_SECONDS) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            text = e.read().decode(errors="replace")
            if e.code in TRANSIENT_HTTP and not last:
                print(f"  {what}: HTTP {e.code} (temporary); "
                      f"retry {attempt + 1}/{len(DELAYS)} in {DELAYS[attempt]}s", flush=True)
                sleep(DELAYS[attempt])
                continue
            if e.code in TRANSIENT_HTTP:
                raise HTTPFailure(e.code, text,
                                  f"{what} (still failing after {len(DELAYS)} retries)") from None
            raise HTTPFailure(e.code, text, what) from None
        except (urllib.error.URLError, socket.timeout, TimeoutError,
                ConnectionError, http.client.HTTPException) as e:
            if not last:
                print(f"  {what}: network error ({e}); "
                      f"retry {attempt + 1}/{len(DELAYS)} in {DELAYS[attempt]}s", flush=True)
                sleep(DELAYS[attempt])
                continue
            raise NetworkFailure(
                f"{what}: network error after {len(DELAYS)} retries: {e}") from None
    raise AssertionError("unreachable")
