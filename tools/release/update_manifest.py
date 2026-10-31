#!/usr/bin/env python3
"""update_manifest.py: the in-game updater's manifest (src/main/updater.cpp) for a release.

    update_manifest.py keygen <private.pem>
        a new Ed25519 key pair (openssl): the private key into <private.pem> (keep it in ~/Vault), the public half
        printed as hex for kPublicKey in src/main/updater.cpp
    update_manifest.py build --version 0.9.2 --tag v0.9.2 [--prerelease] --platform linux-x86_64=<dir>
                             [--platform windows-x86_64=<dir>] [--base <earlier update.json>] --url-base <URL>
                             --assets <dir> --out <update.json>
        every file under each platform's folder (the installed game: Linux's game/, the Windows zip's folder; never
        ROMs/ or .update/) with its size, BLAKE2b-256 and a URL. A file whose hash an earlier manifest (--base) has
        keeps that URL (an earlier release's asset); the others are copied into <assets> as <hash16>-<name>, uploaded
        with this release at <url-base>/<tag>/<asset>. A file both platforms have is uploaded once.
    update_manifest.py sign <update.json> --key <private.pem>
        <update.json>.sig: the Ed25519 signature of the file's bytes, base64 (openssl pkeyutl -rawin)
    update_manifest.py verify <update.json> --public <hex>
        checks a signature (openssl)

make_release.sh runs build and sign for every release it publishes. Full releases take their base from the last full
release (so deleting a test build never breaks a full release's links); test builds from the newest release.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP_DIRS = {"ROMs", ".update"}


def blake2b(path):
    h = hashlib.blake2b(digest_size=32)
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def asset_name(digest, path):
    return f"{digest[:16]}-{re.sub(r'[^A-Za-z0-9._-]', '_', Path(path).name)}"


def files_of(root):
    root = Path(root)
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for f in sorted(files):
            p = Path(dirpath) / f
            if p.is_symlink() or not p.is_file():
                continue
            yield p.relative_to(root).as_posix(), p


def public_hex(private_pem):
    der = subprocess.run(["openssl", "pkey", "-in", private_pem, "-pubout", "-outform", "DER"],
                         check=True, capture_output=True).stdout
    return der[-32:].hex()


def cmd_keygen(a):
    if Path(a.private).exists():
        sys.exit(f"update_manifest.py: {a.private} exists already (never overwrite the release key)")
    Path(a.private).parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["openssl", "genpkey", "-algorithm", "ed25519", "-out", a.private], check=True)
    os.chmod(a.private, 0o600)
    print(public_hex(a.private))


def cmd_build(a):
    known = {}  # hash -> url, from the earlier manifest
    if a.base:
        base = json.loads(Path(a.base).read_text())
        for plat in base.get("platforms", {}).values():
            for f in plat.get("files", []):
                known[f["blake2b"]] = f["url"]
    assets = Path(a.assets)
    assets.mkdir(parents=True, exist_ok=True)
    uploaded = {}
    manifest = {"format": 1, "version": a.version, "tag": a.tag, "prerelease": a.prerelease, "platforms": {}}
    new_bytes = 0
    for spec in a.platform:
        plat, _, folder = spec.partition("=")
        entries = []
        for rel, p in files_of(folder):
            digest = blake2b(p)
            url = known.get(digest) or uploaded.get(digest)
            if url is None:
                name = asset_name(digest, rel)
                target = assets / name
                if not target.exists():
                    try:
                        os.link(p, target)  # the same file, no second gigabyte
                    except OSError:
                        shutil.copyfile(p, target)
                url = f"{a.url_base.rstrip('/')}/{a.tag}/{name}"
                uploaded[digest] = url
                new_bytes += p.stat().st_size
            entries.append({"path": rel, "size": p.stat().st_size, "blake2b": digest, "url": url,
                            "exec": plat.startswith("linux") and os.access(p, os.X_OK)})
        manifest["platforms"][plat] = {"files": entries}
        print(f"update_manifest.py: {plat}: {len(entries)} files")
    Path(a.out).write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"update_manifest.py: {len(uploaded)} new files ({new_bytes / 1048576:.1f} MB) in {assets}; "
          f"{len(known)} known from the base")


def cmd_sign(a):
    with tempfile.TemporaryDirectory() as t:
        sig = Path(t) / "sig"
        subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", a.key, "-in", a.manifest, "-out", str(sig)],
                       check=True)
        Path(a.manifest + ".sig").write_text(base64.b64encode(sig.read_bytes()).decode() + "\n")
    print(f"update_manifest.py: signed {a.manifest} (public key {public_hex(a.key)})")


def cmd_verify(a):
    der_prefix = bytes.fromhex("302a300506032b6570032100")  # SubjectPublicKeyInfo for an Ed25519 key
    with tempfile.TemporaryDirectory() as t:
        pub = Path(t) / "pub.der"
        pub.write_bytes(der_prefix + bytes.fromhex(a.public))
        sig = Path(t) / "sig"
        sig.write_bytes(base64.b64decode(Path(a.manifest + ".sig").read_text()))
        r = subprocess.run(["openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER", "-inkey", str(pub),
                            "-in", a.manifest, "-sigfile", str(sig)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    sys.exit(r.returncode)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("private")
    b = sub.add_parser("build")
    b.add_argument("--version", required=True)
    b.add_argument("--tag", required=True)
    b.add_argument("--prerelease", action="store_true")
    b.add_argument("--platform", action="append", required=True, help="<name>=<folder>")
    b.add_argument("--base")
    b.add_argument("--url-base", required=True)
    b.add_argument("--assets", required=True)
    b.add_argument("--out", required=True)
    s = sub.add_parser("sign")
    s.add_argument("manifest")
    s.add_argument("--key", required=True)
    v = sub.add_parser("verify")
    v.add_argument("manifest")
    v.add_argument("--public", required=True)
    a = ap.parse_args()
    {"keygen": cmd_keygen, "build": cmd_build, "sign": cmd_sign, "verify": cmd_verify}[a.cmd](a)


if __name__ == "__main__":
    main()
