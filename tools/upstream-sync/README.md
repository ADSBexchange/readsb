# upstream-sync

Tooling to triage upstream `wiedehopf/readsb` commits when reconciling this fork.

## Files

- `classify.py` — walks `merge-base(master, upstream/dev)..upstream/dev` and writes a first-pass triage ledger, classifying each commit **take / adapt / skip** by the files it touches.
- `refine.py` — resolves the rows `classify.py` leaves as `needs_unit_test='?'` (non-critical core code) and sets the `commit_strategy` column.
- `commits-ledger.tsv` — the generated ledger. Columns: `sha · date · subject · files · decision · reason · needs_unit_test · needs_devenv · commit_strategy · coupled_to · status`.

## Usage

Requires the `upstream` remote (`git remote add upstream https://github.com/wiedehopf/readsb.git`). Run from anywhere inside the repo:

```bash
git fetch upstream
python3 tools/upstream-sync/classify.py   # regenerates commits-ledger.tsv
python3 tools/upstream-sync/refine.py      # resolves '?' rows + commit_strategy
```

Re-run both after each `git fetch upstream` to re-triage new commits. The output is deterministic (same range → same ledger).
