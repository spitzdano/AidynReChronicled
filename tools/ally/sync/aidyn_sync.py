#!/usr/bin/env python3
"""aidyn_sync.py: set up and check save syncing between machines with Syncthing (its local API; Syncthing must be
running). Used the same way on the PC and the handheld (setup-sync.sh calls it there).

    aidyn_sync.py configure --name NAME    share the save folder (folder id aidyn-saves): only aidyn.mpk and
                                           aidyn-patch.mpk, the last 10 replaced versions kept in .stversions;
                                           removes Syncthing's empty default folder; prints this device's ID
    aidyn_sync.py add-device ID --name NAME   pair with another machine and share the save folder with it
    aidyn_sync.py pending                  machines that tried to connect but aren't paired yet (their IDs)
    aidyn_sync.py status                   this device's ID, the other machines: connected, how much they have
    aidyn_sync.py wait [--timeout S]       until every paired machine is connected and has all the saves
    aidyn_sync.py check                    the same once, as the exit status (0: in sync), quietly
    aidyn_sync.py id                       this device's ID

Saves live in ~/.local/share/AidynReChronicled on every machine. Settings, the stored ROMs, logs and backups there
are not synced (each machine keeps its own). For tests: AIDYN_SYNC_HOME (Syncthing's config folder), AIDYN_SYNC_GUI
(its address) and AIDYN_SYNC_DATA (the save folder) point at another Syncthing instance.
"""
import argparse
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

FOLDER = "aidyn-saves"
LABEL = "Aidyn ReChronicled saves"
DATA = Path(os.environ.get("AIDYN_SYNC_DATA")
            or Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "AidynReChronicled")
IGNORE = ["// Only the saves (aidyn_sync.py): settings, ROMs, logs and backups stay on each machine.",
          "!/aidyn.mpk", "!/aidyn-patch.mpk", "*"]
GUI = os.environ.get("AIDYN_SYNC_GUI", "http://127.0.0.1:8384")


def config_file():
    if os.environ.get("AIDYN_SYNC_HOME"):
        f = Path(os.environ["AIDYN_SYNC_HOME"]) / "config.xml"
        return f if f.exists() else None
    for d in (Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "syncthing",
              Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "syncthing"):
        if (d / "config.xml").exists():
            return d / "config.xml"
    return None


def api_key(wait=60):
    deadline = time.time() + wait
    while True:
        f = config_file()
        if f:
            text = f.read_text()
            if "<apikey>" in text:
                return text.split("<apikey>")[1].split("</apikey>")[0]
        if time.time() > deadline:
            sys.exit("aidyn_sync.py: no Syncthing config found: is Syncthing running? (systemctl --user start syncthing)")
        time.sleep(1)


KEY = None


WAIT = 60  # seconds to wait for Syncthing to answer


def call(method, path, body=None):
    global KEY
    wait = WAIT
    KEY = KEY or api_key(wait)
    data = json.dumps(body).encode() if body is not None else None
    deadline = time.time() + wait
    while True:
        req = urllib.request.Request(GUI + path, data=data, method=method,
                                     headers={"X-API-Key": KEY, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                raw = r.read()
                return json.loads(raw) if raw.strip() else None
        except urllib.error.HTTPError as e:
            if e.code == 404 and method == "GET":
                return None
            sys.exit(f"aidyn_sync.py: Syncthing said {e.code} to {method} {path}: {e.read().decode()[:300]}")
        except (urllib.error.URLError, ConnectionError):
            if time.time() > deadline:
                sys.exit("aidyn_sync.py: Syncthing isn't answering on 127.0.0.1:8384: is it running?")
            time.sleep(1)


def my_id():
    return call("GET", "/rest/system/status")["myID"]


def configure(name):
    me = my_id()
    call("PATCH", "/rest/config/options", {"urAccepted": -1, "startBrowser": False})
    call("PATCH", f"/rest/config/devices/{me}", {"name": name})
    # Syncthing makes an empty "Default Folder" (~/Sync) on its first start: not wanted here.
    for f in call("GET", "/rest/config/folders") or []:
        if f["id"] == "default":
            call("DELETE", "/rest/config/folders/default")
            sync = Path(f["path"]).expanduser()
            if sync.is_dir() and all(p.name == ".stfolder" for p in sync.iterdir()):
                shutil.rmtree(sync)
    DATA.mkdir(parents=True, exist_ok=True)
    folder = call("GET", f"/rest/config/folders/{FOLDER}") or call("GET", "/rest/config/defaults/folder")
    devices = folder.get("devices") or []
    if not any(d["deviceID"] == me for d in devices):
        devices.append({"deviceID": me})
    folder.update({"id": FOLDER, "label": LABEL, "path": str(DATA), "type": "sendreceive", "devices": devices,
                   "fsWatcherEnabled": True, "fsWatcherDelayS": 2, "rescanIntervalS": 3600, "ignorePerms": True,
                   "versioning": {**(folder.get("versioning") or {}), "type": "simple",
                                  "params": {"keep": "10", "cleanoutDays": "0"}}})
    call("PUT", f"/rest/config/folders/{FOLDER}", folder)
    call("POST", f"/rest/db/ignores?folder={FOLDER}", {"ignore": IGNORE})
    print(f"aidyn_sync.py: sharing {DATA} (aidyn.mpk, aidyn-patch.mpk) as {FOLDER!r}; this device is {name!r}:")
    print(me)


def add_device(device, name):
    device = device.strip().upper()
    if len(device.replace("-", "")) != 56:
        sys.exit(f"aidyn_sync.py: {device!r} doesn't look like a Syncthing device ID (8 groups of 7)")
    dev = call("GET", f"/rest/config/devices/{device}") or call("GET", "/rest/config/defaults/device")
    dev.update({"deviceID": device, "name": name, "addresses": dev.get("addresses") or ["dynamic"]})
    call("PUT", f"/rest/config/devices/{device}", dev)
    folder = call("GET", f"/rest/config/folders/{FOLDER}")
    if folder is None:
        sys.exit("aidyn_sync.py: run configure first")
    if not any(d["deviceID"] == device for d in folder["devices"]):
        folder["devices"].append({"deviceID": device})
        call("PUT", f"/rest/config/folders/{FOLDER}", folder)
    print(f"aidyn_sync.py: paired with {name!r} ({device}) and shared the saves with it")


def pending():
    found = call("GET", "/rest/cluster/pending/devices") or {}
    if not found:
        print("aidyn_sync.py: nobody waiting")
    for dev, info in found.items():
        print(f"{dev}  {info.get('name', '')}  {info.get('address', '')}  {info.get('time', '')}")


def status():
    me = my_id()
    print(f"this device: {me}")
    folder = call("GET", f"/rest/config/folders/{FOLDER}")
    if folder is None:
        print("the save folder isn't shared yet (aidyn_sync.py configure)")
        return
    st = call("GET", f"/rest/db/status?folder={FOLDER}")
    print(f"saves: {folder['path']}: {st.get('state')}, {st.get('needTotalItems', 0)} to fetch")
    conns = call("GET", "/rest/system/connections")["connections"]
    names = {d["deviceID"]: d["name"] for d in call("GET", "/rest/config/devices")}
    for d in folder["devices"]:
        if d["deviceID"] == me:
            continue
        c = conns.get(d["deviceID"], {})
        comp = call("GET", f"/rest/db/completion?folder={FOLDER}&device={d['deviceID']}") or {}
        print(f"{names.get(d['deviceID'], '?')}: {'connected' if c.get('connected') else 'not connected'}"
              f"{' (' + c.get('address', '') + ')' if c.get('connected') else ''}, has {comp.get('completion', 0):.0f}%")


def in_sync():
    """(done, message): every other machine sharing the saves connected, nothing left to fetch or send."""
    me = my_id()
    folder = call("GET", f"/rest/config/folders/{FOLDER}")
    st = call("GET", f"/rest/db/status?folder={FOLDER}")
    if st.get("state") != "idle" or st.get("needTotalItems", 0):
        return False, f"saves: {st.get('state')}, {st.get('needTotalItems', 0)} to fetch"
    conns = call("GET", "/rest/system/connections")["connections"]
    for d in folder["devices"]:
        if d["deviceID"] == me:
            continue
        if not conns.get(d["deviceID"], {}).get("connected"):
            return False, "waiting for the other machine to connect"
        comp = call("GET", f"/rest/db/completion?folder={FOLDER}&device={d['deviceID']}") or {}
        if comp.get("completion", 0) < 100:
            return False, f"sending: the other machine has {comp.get('completion', 0):.0f}%"
    return True, "in sync"


def wait(timeout):
    deadline, last = time.time() + timeout, None
    while True:
        done, msg = in_sync()
        if msg != last:
            print(f"aidyn_sync.py: {msg}", flush=True)
            last = msg
        if done:
            return
        if time.time() > deadline:
            sys.exit(f"aidyn_sync.py: not in sync after {timeout} s")
        time.sleep(2)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("configure")
    c.add_argument("--name", required=True)
    a = sub.add_parser("add-device")
    a.add_argument("device")
    a.add_argument("--name", required=True)
    sub.add_parser("pending")
    sub.add_parser("status")
    w = sub.add_parser("wait")
    w.add_argument("--timeout", type=int, default=900)
    sub.add_parser("check")
    sub.add_parser("id")
    args = ap.parse_args()
    if args.cmd == "configure":
        configure(args.name)
    elif args.cmd == "add-device":
        add_device(args.device, args.name)
    elif args.cmd == "pending":
        pending()
    elif args.cmd == "status":
        status()
    elif args.cmd == "wait":
        wait(args.timeout)
    elif args.cmd == "check":
        global WAIT
        WAIT = 3
        try:
            sys.exit(0 if in_sync()[0] else 1)
        except SystemExit as e:
            sys.exit(0 if e.code == 0 else 1)
    else:
        print(my_id())


if __name__ == "__main__":
    main()
