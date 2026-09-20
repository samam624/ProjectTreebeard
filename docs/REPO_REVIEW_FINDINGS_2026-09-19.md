# Repository review findings (2026-09-19)

Adversarial docs-first review of the whole tree, with external confirmation (Codex, consultation
`treebeard-2026-09-19-dirreview`). Findings that were fixed in place are listed with what changed;
findings that could not be fixed from this branch, or that need a maintainer decision, are listed as
open with the evidence needed to close them.

## FIXED

### 1. Five stale documentation links from the `f9fe74d` reorganisation

`TREEBEARD_ADAPTIVE_MOISTURE_CALIBRATION.md` became
`MSV2/docs/MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md` in the MSV1/MSV2/MSV3/DisplayCenter/cloud
reorganisation, and five references were never updated:

| file | issue |
|---|---|
| `DisplayCenter/README.md` | old filename |
| `cloud/docs/SUPABASE_SETUP.md` | old filename |
| `docs/paper/MSV_BUILD_MILESTONE_FACT_CHECK.md` | old filename, twice, in a citation list |
| `docs/STATUS_HANDOFF.md` | old filename **and** wrong directory (`docs/` not `MSV2/docs/`) |

All five repointed. Found by resolving every markdown link relative to its own file, not by
basename matching. Scope of the post-fix result: **zero broken file links among the 111 markdown
files scanned**. Anchors and case-sensitive resolution were not checked, and the scan does not
URL-decode (see CHECKED AND CLEARED).

### 2. This session's charging docs stated the test threshold wrong

They described "the 3.89 V crossing". That collapsed four distinct quantities into one. From
`MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md`:

| quantity | value |
|---|---|
| `VBAT_OV` nominal (VBIAS 1.25 V typ) | 3.887 V |
| `VBAT_OV` on probe 4 (VBIAS 1.2532 V backed out of its own measurement) | ≈3.897 V |
| `VBAT_OV` **ceiling** corner band, ±1 % resistors | 3.857–3.937 V |
| restart, nominal (`VBAT_OV` − `VBAT_OV_HYST`, 35 mV typ) | ≈3.862 V |

3.887 V and 3.897 V are **not** a discrepancy: same fitted resistors (4.42 / 4.12 MΩ), differing
only in whether datasheet VBIAS (1.25 V) or this board's backed-out VBIAS (1.2532 V) is used —
`1.5 × 1.25 × (1+4.42/4.12) = 3.8865` and `1.5 × 1.2532 × (…) = 3.8965`. Neither may be quoted bare
as "the nominal ceiling".

`VBAT_OV_HYST` ranges 18 / 35 / 89 mV, so the **restart** envelope is `3.857 − 0.089` to
`3.937 − 0.018` = **3.768–3.919 V, ~151 mV wide** — a conservative worst case combining independent
extrema, not a measured band. At the observed −53.6 mV/day that gives a **monitoring window of
~2026-09-20 to 09-24**, an extrapolation rather than a guarantee. The observable is the daytime ramp
*returning*, which is supporting evidence; the stronger check is the rework doc's VSTOR
plateau/ripple test.

Corrected in `MSV3_SOLAR_CHARGING_STATE_2026-09-19.md`, `docs/STATUS_HANDOFF.md` and
`MSV3/fleet.json`, with the superseded claim left visible as a dated correction.

### 3. A documented record had been downgraded to a hypothesis

The same session recorded "probe 4 was reworked" as a *leading hypothesis, not confirmed*. It is
documented: `MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` carries a per-board table stating that **all
field probes except `MSV3-MOISTURE-007` were swapped on 2026-09-14 (maintainer)**.

Upgraded to a documented record, with two limits kept explicit: it establishes documented intent, not
an independent physical check of the fitted resistors; and the source doc is on `main` only.

### 4. Probe 4 reading above its own ceiling now has a stated explanation

Probe 4 reads 3973–3984 mV against a ≈3.897 V harvester ceiling, which reads as a contradiction. It
is not: the rework deliberately left the **BQ25170 USB path** at 4.20 V (`R8` = 27 kΩ), and probe 4
was on USB while off-air 2026-09-14 to 09-18. It was charged above the harvester ceiling by a
different path and is descending through the dead band. Recorded in the charging doc and
`fleet.json`, because an unexplained 90 mV excess invites the conclusion that the rework did not
happen.

## OPEN — needs a maintainer decision

### 5. `BATTERY_CURVE` has not been rescaled, five days after the rework

`MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` lists this as "Consequence 1 … **Not yet done**". It is
still not done. The curve maps **4200 mV → 100 %** in both
`DisplayCenter/arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino` and the
deliberately byte-identical copy in `MobileAppPage.h`.

Consequence: a reworked probe at its ≈3.89 V ceiling reports **~63 %** and can never read higher.
Probe 4 currently reports 74–75 % at 3973–3984 mV and is still descending.

**Why this was not simply fixed.** Probe 7 was deliberately *not* reworked and retains a 4.26 V
ceiling. One shared curve can no longer serve both populations: rescaling so 3.90 V = 100 % makes
probe 7 report above 100 %. This needs per-probe charge-profile metadata — plausibly keyed off
`fleet.json`, which already records per-probe hardware state — rather than a clamp or a new constant.
That is a design decision, and it also requires a receiver flash.

### 6. Release-archive integrity failure has escalated

`scripts/verify_msv3_release_archive.py` exits non-zero with one problem:

```text
1.0.3/raw-probes-v65556-manifest.json: DISPUTED, and its bytes have changed AGAIN since the dispute was recorded
  on disk        : 9073af5917e9b5b8ba90e92a32b38b9cf70af83b0fb04bb154bf3bdb890832c6
  in DISPUTED.json: 4a979c491f20c1d3307b733c67ea26a2d8ceef2956c0918f41c01c5a6a6d88b0
```

The file is committed and unmodified in the working tree, so the drift predates this session.
**Deliberately untouched**: the script's own instruction is "Resolve the dispute rather than editing
either side of it," and `DISPUTED.json`'s rule forbids editing recorded hashes.

### 7. Branch divergence is causing incorrect analysis, and is one commit wide

```text
merge-base      b3f9eb7
unique to main  1   (edc9705 "Lower MSV3 BQ25504 VBAT_OV to a ~3.90V storage ceiling")
unique to HEAD  29
```

`edc9705` is the *only* commit this branch lacks, and it contains
`MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` — the authoritative record for the rework this branch's
own analysis depends on. Its absence directly caused findings 2 and 3.

**Integrating `edc9705` is a deliberate future decision, not a quick fix.** A cherry-pick or merge
changes branch history, may conflict with the 29 branch-only commits, and runs against the current
intentionally-uncommitted working tree. It is recorded here with its commit ID so the decision can be
made explicitly. Until then, cite main-only documents **by commit**, as external evidence.

### 8. A datasheet limit is mis-stated on `main`

`MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` says *"TI caps the sum at 10 MΩ"* and rejects two divider
options for "sum > 10 MΩ".

The BQ25504 states both things, and the safest form is the literal one: **the application-section
prose recommends "no higher than 10 MΩ"; the Recommended Operating Conditions table (§7.3) lists
9–11 MΩ** (MIN 9 / NOM 10 / MAX 11) for `ROV1+ROV2`, `RUV1+RUV2` and `ROK1+ROK2+ROK3`. Quote both
rather than synthesising a single cap.

**The decision survives** — the rejected candidates were 11.50 and 11.52 MΩ, above the 11 MΩ maximum
as well. Only the stated reason is wrong. Not fixed here because the document exists only on `main`.

## CHECKED AND CLEARED

- **MSV4's divider sums are in spec.** 10.35 / 10.02 / 10.38 MΩ initially looked like violations of
  the "10 MΩ cap" cited by the MSV3 doc. Against the actual §7.3 range of 9–11 MΩ they are fine.
  Here the *newer* document was right and the older one wrong. The arithmetic is not the open part:
  at ±1 % the worst case is `10.38 × 1.01 = 10.484 MΩ`, still under 11 MΩ. What remains to confirm
  before assembly is BOM/stuffing/topology accuracy — that the fitted parts and network are what the
  table says.
- **`MSV1/EEC284_Final_Paper (2).pdf` links are not broken.** Two links spell it
  `EEC284_Final_Paper%20%282%29.pdf`; that is valid URL encoding and resolves. **Known checker
  limitation, not fixed:** the scan used here does not URL-decode before resolving, so it reports
  these two as broken. Any future link check needs decoding added before its output is trusted.
- **~40 "missing" referenced files** (`cloud.json`, `ui.json`, `device.json`, `wifi.json`,
  `out.json`, `manifest.json`, `bench.zip`) are device-resident SPIFFS/NVS artifacts or transient
  command outputs, not repository deliverables.

## Method

Every markdown link in 110 files resolved against the filesystem relative to its own directory; a
separate basename-index pass over 87 docs for non-link file references; `git log --follow` to
confirm renames; the BQ25504 extracted datasheet read directly rather than trusting either citing
document; `git show main:<path>` to read main-only evidence; and the repository's own
`verify_msv3_release_archive.py`.
