#!/usr/bin/env python3
"""Pre-classify upstream readsb commits into a triage ledger (take/adapt/skip).

Walks merge-base(master, upstream/dev)..upstream/dev and classifies each commit
by the files it touches: core decode/output paths -> take (test-backed);
debian/changelog & release tooling -> skip; ADSBx-customized build files ->
adapt; etc. Output is a first-pass ledger (commits-ledger.tsv); run refine.py
afterwards to resolve the needs_unit_test='?' rows and the commit_strategy.
Re-run both after each `git fetch upstream` to re-triage new commits.

Requires the `upstream` remote (https://github.com/wiedehopf/readsb.git).
Run from anywhere inside the readsb repo/worktree.
"""
import subprocess, csv, re, os

REPO = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True, check=True).stdout.strip()
LEDGER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "commits-ledger.tsv")

def git(*args):
    return subprocess.run(["git", "-C", REPO, *args],
                          capture_output=True, text=True, check=True).stdout

mb = git("merge-base", "master", "upstream/dev").strip()
rng = f"{mb}..upstream/dev"

raw = git("log", "--reverse", "--no-merges",
          "--pretty=format:@@@%H\t%cI\t%s", "--name-only", rng)

# ---- parse into [(sha, date, subject, [files])] ----
commits = []
cur = None
for line in raw.splitlines():
    if line.startswith("@@@"):
        if cur: commits.append(cur)
        sha, date, subj = line[3:].split("\t", 2)
        cur = (sha, date, subj, [])
    elif line.strip() and cur:
        cur[3].append(line.strip())
if cur: commits.append(cur)

# ---- file bucketing ----
CRITICAL = ("mode_s", "track", "cpr", "net_io", "json_out", "globe_index",
            "api", "aircraft", "comm_b", "icao_filter", "receiver", "demod")

def bucket(f):
    if f == "version": return "VERSION"
    if f in ("tag.sh", "nogrind.sh", "changelog") or \
       (f.endswith(".sh") and f not in ("buildToS3.sh", "checkapi.sh")): return "TOOLING"
    if f == "debian/changelog": return "CHANGELOG"
    if f.startswith("debian/"): return "PACKAGING"  # rules/control/service/manpages — ADSBx does NOT fork debian/
    if f in ("Makefile", "Dockerfile", ".dockerignore", "required_packages") \
       or f.startswith(".github/") or f.startswith("docker"): return "BUILD"
    if f.endswith(".md") or f.startswith("README"): return "DOCS"
    if re.search(r"(sdr_|rtlsdr|bladerf|hackrf|soapy|airspy|plutosdr|limesdr)", f): return "SDR"
    if f.startswith(("compat/", "minilzo/", "uat2esnt/")): return "VENDORED"
    if f.endswith("_tests.c") or f.startswith("tests/") or f == "unittests.c": return "TESTS"
    if f.endswith((".c", ".h")): return "CORE"
    return "OTHER"

FOLLOWUP = re.compile(r"\b(fix|oops|typo|revert|actually|redo|hotfix|broke|"
                      r"regression|undo|wrong|again|forgot|missing)\b", re.I)

rows = []
for sha, date, subj, files in commits:
    bset = {bucket(f) for f in files}
    nonver = bset - {"VERSION"}

    decision = reason = ""
    unit = dev = ""

    if not nonver:
        decision, reason = "skip", "version bump only"
    elif nonver <= {"CHANGELOG"}:
        decision, reason = "skip", "debian changelog metadata only"
    elif "PACKAGING" in bset and "CORE" not in bset and re.search(r"sdr", subj, re.I):
        decision, reason = "adapt", "debian SDR build profile may affect dpkg-buildpackage -Prtlsdr; verify"
    elif nonver <= {"PACKAGING", "CHANGELOG"}:
        decision, reason, unit = "take", "debian packaging (ADSBx does not fork debian/); keeps deb build current", "n"
    elif nonver <= {"TOOLING", "DOCS", "CHANGELOG"}:
        decision, reason = "skip", "upstream docs/release tooling; ADSBx uses own CI/README"
    elif nonver <= {"VENDORED"}:
        decision, reason, unit = "take", "vendored lib only", "n"
    elif nonver <= {"BUILD", "CHANGELOG"}:
        decision, reason = "adapt", "build/CI only; preserve ADSBx label+workflows"
    elif nonver <= {"SDR", "PACKAGING", "CHANGELOG", "DOCS"}:
        decision, reason, unit = "take", "SDR/hardware path; low test priority", "n"
    elif nonver <= {"TESTS"}:
        decision, reason, unit = "take", "upstream test change", "n"
    elif "CORE" in bset:
        crit = any(any(c in f for c in CRITICAL) for f in files)
        decision = "take"
        if crit:
            reason, unit = "core decode/output path (critical)", "y"
        else:
            reason, unit = "core code (non-critical)", "?"
        if "BUILD" in bset:   # only OUR customized build files need the tip-patch resolve; debian/ is not forked
            decision = "adapt"
            reason += " + touches build (resolve to ADSBx side)"
    else:
        decision, reason = "REVIEW", f"unclassified buckets: {sorted(nonver)}"

    coupled = "follow-up?" if FOLLOWUP.search(subj) else ""

    if len(files) <= 4:
        fsum = ",".join(files)
    else:
        top = {}
        for f in files:
            k = f.split("/")[0] if "/" in f else f
            top[k] = top.get(k, 0) + 1
        fsum = f"{len(files)} files: " + ",".join(sorted(top))

    rows.append({
        "sha": sha[:9], "date": date[:10], "subject": subj[:80],
        "files": fsum, "decision": decision, "reason": reason,
        "needs_unit_test": unit, "needs_devenv": dev,
        "commit_strategy": "", "coupled_to": coupled, "status": "",
    })

cols = ["sha","date","subject","files","decision","reason",
        "needs_unit_test","needs_devenv","commit_strategy","coupled_to","status"]
with open(LEDGER, "w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=cols, delimiter="\t")
    w.writeheader(); w.writerows(rows)

from collections import Counter
dec = Counter(r["decision"] for r in rows)
print(f"merge-base: {mb[:9]}  range: {rng}")
print(f"total commits: {len(rows)}")
for k in ("take","adapt","skip","REVIEW"):
    print(f"  {k:7} {dec.get(k,0)}")
print(f"ledger -> {LEDGER}")
print("next: run refine.py to resolve needs_unit_test='?' rows")
