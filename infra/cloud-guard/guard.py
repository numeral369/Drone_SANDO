"""SANDO demo cloud guard (DigitalOcean Function, runs every 5 minutes inside DO).

Deletes any droplet tagged `sando-demo` that
  * has no `sando-deadline-<unix>` tag, or
  * is past its deadline, or
  * is older than MAX_SESSION_MIN minutes.
This works even when the operator's PC is off. Uses only the standard library.
"""
import json
import time
import urllib.request
from datetime import datetime

API = "https://api.digitalocean.com/v2"
TAG = "sando-demo"
MAX_SESSION_MIN = 180


def _req(method, path, token):
    r = urllib.request.Request(API + path, method=method,
                               headers={"Authorization": "Bearer " + token,
                                        "Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=20) as resp:
        body = resp.read()
        return json.loads(body) if body else {}


def main(args):
    token = args.get("token")
    if not token:
        return {"body": {"error": "no token"}}
    now = time.time()
    actions = []
    data = _req("GET", "/droplets?tag_name=%s&per_page=200" % TAG, token)
    for d in data.get("droplets", []):
        created = datetime.strptime(d["created_at"], "%Y-%m-%dT%H:%M:%SZ")
        created_ts = (created - datetime(1970, 1, 1)).total_seconds()
        deadline = None
        for t in d.get("tags", []):
            if t.startswith("sando-deadline-"):
                try:
                    deadline = int(t[len("sando-deadline-"):])
                except ValueError:
                    pass
        why = None
        if deadline is None:
            why = "no-deadline-tag"
        elif now >= deadline:
            why = "deadline-passed"
        elif (now - created_ts) / 60.0 > MAX_SESSION_MIN:
            why = "max-session-exceeded"
        if why:
            _req("DELETE", "/droplets/%d" % d["id"], token)
            actions.append({"deleted": d["name"], "id": d["id"], "reason": why})
        else:
            actions.append({"kept": d["name"], "seconds_left": int(deadline - now)})
    return {"body": {"checked_at": int(now), "actions": actions}}
