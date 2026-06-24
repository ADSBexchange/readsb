#!/usr/bin/env python3
"""AX-936: resolve the needs_unit_test='?' rows (non-critical core code) to y/n.

classify.py leaves '?' on core-code commits that don't match a critical-path
pattern. This script applies a human-reviewed decision map: 'y' where the
commit changes assertable behavior with a test home in the ADSBx suite, 'n'
for SDR autogain tuning, terminal UI, help/docs text, perf/refactor, Apple
build shims, and debug/log changes. Run after classify.py.
"""
import csv, os
from collections import Counter

LEDGER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "commits-ledger.tsv")
Y = "y"

# sha9 -> (needs_unit_test, needs_devenv, note) -- assertable behavior + test home
DEC = {
    "1a4c123d2": (Y, "y", "exit-on-hang signaling; cover via test_lifecycle"),
    "3da4c74de": (Y, "",  "new --devel=legacy_history output mode"),
    "6ee23d772": (Y, "",  "uptime in stats output; test_stats_and_history"),
    "85c087779": (Y, "",  "WMM2025 model: geomag_tests expected values MUST update"),
    "7cbbbe51f": (Y, "",  "setLatLon affects position/distance output"),
    "0e5dc1197": (Y, "",  "creates --write-json dir; test_output_formats/lifecycle"),
    "2b5abcc6d": (Y, "",  "greatcircle distance calc change; util_tests"),
    "8cac43bcd": (Y, "",  "getUptime no synthetic ts; stats output"),
    "f743b90e7": (Y, "",  "ais_charset NUL-termination; ais_charset_tests"),
    "7335e0c01": (Y, "",  "distance calc back to elliptical earth; util_tests"),
    "079768749": (Y, "y", "synthetic clock/priority affects ifile replay determinism in tests"),
    "7d341c6be": (Y, "",  "addrtype_t OOB fix; aircraft type output correctness"),
    "b2e7605da": (Y, "",  "dated sub-folder output path feature"),
}
N_NOTES = {
    "autogain": "SDR autogain tuning; not unit-testable, validate on SDR build if used",
    "ui":       "viewadsb/interactive terminal UI; not in automated suite",
    "help":     "help/README text only",
    "perf":     "perf/memory; no observable output change",
    "refactor": "cleanup/refactor; no behavior change",
    "apple":    "Apple/OSX build-portability shim; n/a to Linux prod",
    "beast":    "beast/SDR serial path; not unit-testable",
    "debug":    "debug/log output only",
    "testfix":  "fixes make test target itself; test plumbing not new coverage",
}
N_CAT = {
    "62768aadb":"refactor","e62917e0d":"perf","d6932bb08":"debug","ae09ee115":"refactor",
    "f40bd618d":"autogain","1e276882a":"autogain","2c51b73c5":"autogain","80fc54619":"autogain",
    "8fd5acf09":"autogain","89fc77451":"autogain","b4bd2f5c6":"autogain","7be4ddf30":"autogain",
    "15823dd47":"autogain","7c34ff39b":"autogain","e8de53682":"autogain","2b6338496":"help",
    "fb8449754":"autogain","795d69a37":"beast","8d2c17718":"debug","21dd45e9e":"apple",
    "81ef113b1":"debug","a5f3c4009":"apple","435bdb8c1":"autogain","5c32b8bc6":"refactor",
    "b251391b5":"refactor","171caff26":"perf","6ec88bffc":"refactor","0ec418431":"debug",
    "bbf446ad0":"help","3df53b3ab":"perf","46ad2fc16":"help","9e5d95f7c":"perf",
    "5bf3030c9":"perf","6a9f6432c":"beast","ece78830a":"beast","d3eb56779":"perf",
    "e5a4fe909":"autogain","3862d1d5a":"autogain","1ecee5743":"autogain","cd775cba7":"perf",
    "85d434726":"autogain","b1487aca5":"autogain","451221d54":"autogain","dab71ee4a":"refactor",
    "21d399deb":"refactor","25eb72f0d":"ui","4493ff430":"ui","f4d29c888":"ui",
    "74f2d9d01":"ui","de952d873":"ui","b6c96747b":"ui","eaa4085fa":"ui","c77b06279":"ui",
    "539e11d46":"ui","26d725d3e":"help","460c9fc6b":"debug","3852ad21e":"debug",
    "83cfa54e1":"help","2d007af3c":"ui","97ba5e692":"perf","14881d22a":"debug",
    "5831f9109":"autogain","9a9da7a14":"debug","eb635963c":"debug","ba2fae14f":"refactor",
    "0dfa2aafa":"perf","542534546":"perf","15917d77a":"refactor","175bf2472":"testfix",
}
DEVENV_EXTRA = {"cd775cba7": ("y", " (validate default change in dev)")}

rows = list(csv.DictReader(open(LEDGER), delimiter="\t"))
cols = list(rows[0].keys())
changed = 0
for r in rows:
    if r["needs_unit_test"] != "?":
        continue
    sha = r["sha"]
    if sha in DEC:
        ut, dev, note = DEC[sha]
        r["needs_unit_test"] = ut
        if dev: r["needs_devenv"] = dev
        r["reason"] += " | " + note
        changed += 1
    elif sha in N_CAT:
        r["needs_unit_test"] = "n"
        r["reason"] += " | " + N_NOTES[N_CAT[sha]]
        if sha in DEVENV_EXTRA:
            r["needs_devenv"], extra = DEVENV_EXTRA[sha]
            r["reason"] += extra
        changed += 1
    else:
        print(f"!! unmapped '?' sha: {sha} {r['subject'][:50]}")

# ---- commit-strategy pass ----
# Our entire build customization is 3 isolated, test-neutral files. So we
# revert it at the start of the walk, take upstream verbatim (clean patch-ids),
# and re-apply the customization as a single tip commit. That turns build-only
# "adapt" rows into patch-id-clean "take" rows whose build hunks are reconciled
# by the tip patch -- only the debian SDR-profile commits remain true adapts.
retagged = 0
for r in rows:
    if r["decision"] != "adapt":
        continue
    if "SDR build profile" in r["reason"]:
        r["commit_strategy"] = "verify-packaging"  # take verbatim; verify dpkg-buildpackage -Prtlsdr output
    else:
        r["decision"] = "take"
        r["commit_strategy"] = "tip-patch"  # ADSBx build customization re-applied in single tip commit
        r["reason"] += " | retag->take: build hunks reconciled by tip customization patch"
        retagged += 1

with open(LEDGER, "w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=cols, delimiter="\t")
    w.writeheader(); w.writerows(rows)

dec = Counter(r["decision"] for r in rows)
strat = Counter(r["commit_strategy"] for r in rows if r["commit_strategy"])
c = Counter(r["needs_unit_test"] for r in rows if r["decision"] in ("take","adapt"))
print(f"resolved {changed} '?' rows; remaining '?': {sum(1 for r in rows if r['needs_unit_test']=='?')}")
print(f"retagged build adapts -> take: {retagged}")
print(f"decision: take={dec['take']} adapt={dec['adapt']} skip={dec['skip']}")
print(f"commit_strategy: {dict(strat)}")
print(f"needs_unit_test across take+adapt: y={c['y']} n={c['n']}")
