# Deployment & Operations Document

**System:** GPU Fabric Congestion Simulator & Router
**Scope:** How the system is built, verified, executed, and its artifacts
managed — on a developer workstation, in CI, and (optionally) on a batch/HPC
node for large sweeps.

This is a single-process research simulator: "deployment" means *reproducible
build + verified execution + durable artifacts*, not servers. There is
deliberately **no** network service, no daemon, no container requirement, and
no cloud dependency in the result-producing path.

---

## 1. Environments

| Environment | Purpose | Build flavor | Trigger |
|---|---|---|---|
| Developer workstation | Day-to-day development, single runs | `build-san` (Debug + ASan/UBSan) for tests, `build` (Release) for runs | manual |
| CI (GitHub Actions, `ubuntu-24.04`) | Merge gate: sanitized tests + end-to-end artifact check | both | every push to `main` and every PR |
| Batch/HPC node (optional, Phase 3+) | Seeded sweeps: hundreds of independent runs | Release | manual / job scheduler |

Supported platform: Linux x86-64. The code is standard C++20 with no
platform-specific calls, but Linux is the only *verified* platform; results
intended for publication must come from a verified platform.

## 2. Prerequisites

| Dependency | Minimum | Notes |
|---|---|---|
| CMake | 3.20 | |
| C++ compiler | GCC 13 / Clang 16 | C++20 required; CI pins GCC on ubuntu-24.04 |
| Python | 3.10 | 3.12 verified |
| Python packages | `requirements.txt` | pyyaml, pyarrow, pandas, matplotlib, numpy — analysis side only |
| git | any recent | run manifests embed the SHA; runs outside a git checkout are stamped `nogit` and are not publishable |

The C++ core has **zero third-party dependencies** by design (ADR-002,
ADR-007). Only the Python analysis layer has a dependency footprint, isolated
in `.venv/`.

## 3. Build matrix

```sh
# Sanitized test build — the merge gate
cmake -B build-san -DCMAKE_BUILD_TYPE=Debug -DGPUFAB_SANITIZE=ON
cmake --build build-san -j
ctest --test-dir build-san --output-on-failure

# Release build — the only build results may be produced with
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Both flavors compile with `-Wall -Wextra -Wpedantic -Werror`. `GPUFAB_SANITIZE`
adds ASan+UBSan to every target including tests. Never publish numbers from a
sanitized build (instrumentation distorts nothing in virtual time, but the
policy keeps the provenance rule simple: **Release binary, clean git tree**).

Python environment (one-time per checkout):

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

## 4. CI/CD pipeline

Definition: `.github/workflows/ci.yml`. Two independent jobs on every push/PR:

```
push / PR
  ├─ sanitized-tests:  cmake (Debug, ASan+UBSan) → build → ctest
  └─ end-to-end:       cmake (Release) → build
                       → pip install -r requirements.txt
                       → python python/run.py configs/phase1_smoke.yaml
                       → assert all 7 run artifacts exist
                          (config.yaml, sim.cfg, manifest.json, seeds.txt,
                           flows.csv, flows.parquet, fct_cdf.png)
```

Policy:
- Both jobs are required; a red pipeline blocks merge.
- The workflow consumes **no** GitHub event-derived strings in `run:` steps
  (no title/body/branch-name interpolation), which closes the
  workflow-injection class of attacks by construction. Keep it that way when
  editing the workflow.
- No deployment step exists or should be added: CI verifies, it does not
  publish. Releases are git tags (see §7).

## 5. Running experiments (operations)

### 5.1 Single run

```sh
.venv/bin/python python/run.py configs/<name>.yaml
```

One command = one run directory = (eventually) one figure. `run.py` refuses to
overwrite: each invocation creates a fresh
`results/<utc>_<confighash8>_<gitsha8>/`.

Operational rules:
1. **Configs are code.** Every config that produces a kept figure lives in
   `configs/` and is committed. Ad-hoc uncommitted configs are for exploration
   only.
2. **Publishable = clean tree.** If `manifest.json` says `git_dirty: true`,
   the run is exploratory by definition. Commit first, rerun.
3. **Never hand-edit a run directory.** Regenerate it; that is the point of
   the system.

### 5.2 Sweeps (Phase 3+)

The harness will orchestrate N independent `gpufab_sim` processes (one per
seed × config point). Because runs share nothing but read-only inputs, sweep
parallelism is trivial process-level parallelism — `xargs -P`/GNU parallel on
a workstation, or one scheduler task per run on a batch node. Determinism is
per-process, so parallel execution cannot perturb results.

Sizing guidance (packet-train model, Release build): a 1024-node all-reduce
replay is a seconds-scale, single-core, sub-GB run. A full sweep
(e.g. 3 routers × 4 load points × 20 seeds = 240 runs) is minutes on a
16-core machine. Plan disk at ~1–50 MB per run directory once Phase 2
link-level time series land.

### 5.3 Regenerating a figure

```sh
git checkout <sha-from-manifest>
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
.venv/bin/python python/run.py <config-from-run-dir>
```

Byte-identical `flows.csv`/`seeds.txt` is the acceptance check; a mismatch is
a release-blocking bug in the determinism invariant.

## 6. Results-store management

- `results/` is **gitignored**; provenance lives in the manifest + tracked
  configs, not in committing outputs.
- Retention: exploratory runs are disposable at will. Runs backing a figure
  in the README/writeup are archived (tar the run directory) and referenced
  by their directory name, which encodes timestamp + config hash + git SHA.
- The Parquet file is the canonical analysis input; `flows.csv` is kept as
  the raw engine output for debugging and diffing.

## 7. Versioning and release policy

- Trunk-based development on `main`; CI green is the merge bar.
- Phase completions are annotated git tags (`phase-1`, `phase-2`, …) so every
  claim in the writeup pins to a tag.
- The README's headline result section is updated only at phase gates, and
  the scale claim is fixed policy: **1024 nodes simulated, validated at
  16–128** — never beyond what was actually run.

## 8. Troubleshooting runbook

| Symptom | Likely cause | Action |
|---|---|---|
| `gpufab_sim: fatal: config: missing key …` | Config schema drift vs binary version | Diff your YAML against README §Configuration reference; rebuild if the binary predates the key |
| `binary not found at build/gpufab_sim` from run.py | Release build absent | `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` |
| `ModuleNotFoundError: pyarrow` (etc.) | venv not activated/installed | `.venv/bin/pip install -r requirements.txt`; invoke via `.venv/bin/python` |
| Two runs of the same config differ | Determinism invariant broken — release-blocking | Bisect to the offending commit; suspects: float in time math, unlogged RNG, container-iteration order, uninitialized read (run the sanitized build) |
| ASan/UBSan failure in CI but not locally | You built without `GPUFAB_SANITIZE=ON` | Reproduce with the `build-san` recipe above |
| `run.py` exits nonzero, no Parquet | Engine aborted; run dir holds `sim.cfg` for repro | Rerun the binary directly: `build/gpufab_sim <run-dir>/sim.cfg /tmp/out` under the sanitized build |
| Run stamped `nogit` or `git_dirty: true` | Outside a checkout / uncommitted changes | Fine for exploration; commit before producing figures |

## 9. Security posture

Attack surface is minimal by construction: no network listeners, no secrets,
no privileged operations; inputs are local YAML files parsed into flat
key-value pairs; outputs are local files. The two standing rules:
1. CI workflow steps never interpolate untrusted event data (§4).
2. Third-party dependency review applies only to `requirements.txt`; the C++
   core stays dependency-free.
