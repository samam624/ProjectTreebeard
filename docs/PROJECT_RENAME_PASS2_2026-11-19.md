# Project rename, pass 2 (2026-11-19), and the plan for what is left

Follows [the 2026-09-02 pass](../DisplayCenter/docs/PROJECT_RENAME_MSV2_TO_TREEBEARD_2026-09-02.md).
That pass renamed sketch folders and `#define`s; the user-visible text and a few paths were left
behind, and some later work used the old name again. Its rule still holds: **"MSV2" is also a real
probe generation** (`MSV2/`, sensors 1 and 2, `MSV2_Moisture_*`). Only *project-name* uses change.

## Done in this pass (no wire, database or persisted identifier touched)

| Area | Change |
|---|---|
| App name | **`Display Center`** (maintainer's choice, 2026-11-20; a first try, "Treebeard Plants", was rejected). Used for: receiver setup-screen header, title fallbacks, factory-default `ui.json` title (was `MSV2 Garden`), receiver phone app title/manifest, cloud dashboard title/header/manifest, Edge Function manifest, mock server. A saved title is untouched: receiver #1's `ui.json` title is "Plant Monitoring Center" and stays. |
| Receiver screen, project-level text | `MSV2 booting` / `MSV2 display test` -> `Treebeard ...` |
| Receiver phone app (`MobileAppPage.h`) | page title, header, title fallbacks, local manifest name/short name/description |
| Receiver internals | header comment, serial ready lines, `Open Treebeard setup at ...` redirect body, FreeRTOS task names `tb_cloud_sync` / `tb_ble_scan` (kept under the 16-char limit), TFT_eSPI `USER_SETUP_INFO`, local BLE name `TREEBEARD-DISPLAY` (never advertised: the command beacon carries flags + manufacturer data only, scan response off) |
| Receiver build tag | `Treebeard runtime build 2026-11-19 rename-pass2-v2` (own tag, so no binary with different strings reports `sound-choice-v1`; `-v1` was the short-lived "Treebeard Plants" build) |
| Other DisplayCenter sketches | SWD flasher, antenna test (BLE name `TB-ANTTEST`, scanned by nothing), identity dump, buzzer audition banners |
| Folders (`git mv`, history follows) | `DisplayCenter/hardware/MSV2_ESP32_DisplayCenter/` -> `Treebeard_ESP32_DisplayCenter/`; `DisplayCenter/arduino/MSV2_ESP32_Buzzer_Audition/` -> `Treebeard_ESP32_Buzzer_Audition/` |
| Cloud dashboard | manifest name/short name, fallback title and QR error text; bundle bumped `app.v18.js` -> `app.v20.js` (v19 was the short-lived "Treebeard Plants" deploy) and SW cache `msv2-cloud-app-v20` because `/assets/*` is cache-first/immutable (an in-place edit would never reach installed clients) |
| `plant-dashboard` Edge Function | role string, error page title, manifest |
| Config | removed the dead `MSV2_RECEIVER_TOKEN` line from `cloud/supabase/.env.example` (no function has read it since 2026-07-07) |
| Docs, skills, scripts | titles of STATUS_HANDOFF / PRODUCT_ROADMAP / SUPABASE_SETUP, two skill descriptions, help text of 8 scripts, mock cloud server branding |

**Why the hardware folder could move after all.** The 09-02 doc kept it as "the Altium project,
bound to an external CAD path". Checked 2026-11-19: the repo folder holds no `.PrjPcb`/`.SchDoc`/
`.PcbDoc`; the Altium project lives outside the repo at
`C:\Users\Public\Documents\Altium\MSV2_ESP32_DisplayCenter`, and the STEP files carry the name only
as a product label. The repo folder holds exports, so moving it does not touch Altium. Cost: GitHub
links to the old path from outside the (public) repo break; in-repo links were updated.

**Deliberately left as they are**

- Everything on the 09-02 keep-list (BLE magic and company ID, receiver ID prefix, Supabase
  objects and migration names, `msv2.*` storage keys, SW cache-key prefix, `MSV2_*` wire constants).
- Filenames inside the dated print releases under `V2/output/` (`MSV2_DisplayCenter_*.3mf`): they
  are listed in `SHA256SUMS_*` and the release reports. They are released artifacts, not branding.
- Dated docs, build tags in history, the MSV2 *generation* everywhere.
- The external Altium project's own name (an Altium-side rename, outside this repo).
- `MSV2-Display` setup SSID and the Netlify site name: Tier 3 below.

---

# Tier 3 plan (A done 2026-11-20; B not started, needs the maintainer's go-ahead)

## A. Setup Wi-Fi name `MSV2-Display`

> **DONE 2026-11-20, build `rename-pass2-v3`.** New name **`Display-Center`** (matches the app name
> the maintainer chose). `AP_SSID` is now the only literal: the phone app's 6 uses go through
> `setupSsidName()`, which reads `setupSsid` from `GET /wifi`. Checked before flashing:
> `/wifi.json` stores only `ssid`, `password` and `localOnly`, so no saved setting names the AP.
> Docs carry "(`MSV2-Display` before `rename-pass2-v3`)". Flashed receiver #1 (COM16) after the
> outbox drained; afterwards `/device` reports `rename-pass2-v3`, `/wifi` reports
> `setupSsid: Display-Center` in station mode, `/app` serves both `setup-ssid` spans, and a cloud
> upload succeeded with 0 TLS failures. **Not observed:** the AP itself on air and a phone joining
> it. The receiver is in station mode, which raises no AP, and forcing setup would take it
> offline. Check on DisplayCenter #2's first boot (step 5).

**What it touches.** `AP_SSID` at `Treebeard_ESP32_Display_Runtime.ino:221` is the single source;
the firmware already publishes it as `setupSsid` in two JSON responses. Hard-coded copies:
the header comment, the TFT setup screen (`Join network: MSV2-Display`), 7 help strings in
`MobileAppPage.h`, `DisplayCenter/README.md` (5), `DisplayCenter/docs/ESP32_DISPLAY_FLASHING.md`
(5), `docs/PRODUCT_ROADMAP.md` (3), `DisplayCenter/scripts/push_display_ui.py` (2). Dated docs and
the migration 021 comment stay.

**Who notices.** Only someone in setup or local-only mode: a phone that saved `MSV2-Display` has to
join the new network once. A receiver on home Wi-Fi does not raise the AP at all.

**Steps**

1. Decide the name. Suggest `Treebeard-Display` (the AP is also the local-only dashboard, so
   "Setup" would be wrong half the time). **Maintainer's call.**
2. Change `AP_SSID`; make the 7 phone-app strings render `setupSsid` from the device JSON instead
   of a literal, so a future rename is one line. Update the TFT string and check its width on the
   480 px panel.
3. Before flashing, confirm nothing persists the AP name (`/wifi.json` should hold station
   credentials only), so the change cannot strand a stored setting.
4. New build tag; full regression gate; compile.
5. Flash **DisplayCenter #2 first** (it is being built, so it never ships the old name), using
   `-ForceWifiSetup` on the bench to see the AP, join it from a phone, confirm the captive portal
   opens and `/device` reports the new `setupSsid`.
6. Flash receiver #1 on its next routine update (drain the history outbox to zero first; see the
   09-02 note on `LittleFS.begin(true)`).
7. Docs say `Treebeard-Display` and add "(`MSV2-Display` on firmware before `<tag>`)" while any
   receiver still runs older firmware.

**Size.** About an hour of edits plus one flash per receiver. Low risk.

## B. Netlify site `msv2-plants.netlify.app`

**What it touches (measured).**

- Compiled into every receiver: `TREEBEARD_HOSTED_APP_URL` (`.ino:238`) builds `appUrl` and the
  pairing claim link `...?rid=<id>&code=<claim>` (`.ino:3614-3619`) shown as the QR. Every
  receiver keeps pointing at the old URL until it is reflashed.
- Public links: `README.md` (3), `cloud/README.md`, `cloud/docs/SUPABASE_SETUP.md` (3),
  `docs/portfolio/START_HERE.md`, plus share links already sent to people.
- Supabase functions answer with `Access-Control-Allow-Origin: *`, so there is **no origin
  allow-list to update**.

**The real cost is browser storage, not DNS.** Storage is per-origin. A household's share token
lives in `localStorage["msv2.householdShareToken"]` and the `msv2_share_token` cookie on
`msv2-plants.netlify.app`. A new origin starts empty, so every phone and installed PWA would lose
its household link and need the share link or QR again, unless the token is handed over.

**Steps**

1. Decide the name: `treebeard-plants.netlify.app`, or a custom domain (costs a domain, but makes
   this the last rename). **Maintainer's call.**
2. Confirm in Netlify's current docs what happens to the old subdomain on a site rename. Assumed
   (not verified): it stops serving. Either way, do not rename the existing site. **Create a new
   site** deploying the same `cloud/web/plant-dashboard` dir (`netlify.toml` publish path), and run
   `verify_plant_dashboard_qr.mjs` and the demo against it.
3. Turn the old site into a hand-off:
   - an `index.html` that reads the old token (localStorage + cookie) and forwards to the new
     origin with the token in the URL **fragment** (`#`, never sent to a server), carrying any
     `rid`/`code`/`receiverId`/`claimCode` query through verbatim;
   - a small change in the new app: accept the token from the fragment, store it, then strip it
     with `history.replaceState`;
   - a self-unregistering `sw.js`, so installed PWAs stop serving the old cached shell. The app
     shell is already network-first, so the new `index.html` will be fetched.
   - Security note: the fragment shows up in that tab's history. If that is not acceptable, the
     alternative is an iframe + `postMessage` hand-off, which is more work.
4. Firmware: change `TREEBEARD_HOSTED_APP_URL`, new tag, flash receivers (can ride with A).
   Receivers on old firmware keep working through the hand-off.
5. Update README/doc links; dated docs stay.
6. **Keep the old site up indefinitely.** Old QR codes, sent share links and old firmware all
   point at it.

**Size.** About half a day plus two deploys and one flash per receiver. Medium risk, almost all of
it in step 3. Do A and B in the same receiver flash if both are approved.

## Never (unchanged from 09-02, restated because the cost grew)

- **BLE magic `MSV2` + company ID:** MSV3 and MSV4 production firmware also send it now. Renaming
  means reflashing every probe of every generation in lockstep, including the outdoor MSV2.
- **Receiver ID prefix `MSV2%012llX`:** the receiver's cloud identity; history, calibration and
  share links hang off it.
- **Supabase `msv2_*` (~60 tables/RPCs/indexes) and the 43 migration filenames:** recreating every
  function body for a name nobody sees. Migration filenames must never change.
- **`msv2.*` storage keys / `msv2_share_token` cookie:** renaming logs every household out unless a
  migration is written, for no visible gain.

## Verification results (2026-11-19/20)

- All five touched DisplayCenter sketches compile (receiver, SWD flasher, antenna test, identity
  dump, buzzer audition). The first compiles used a scratch copy of the build script with one
  check skipped, because `verify_msv4_packet_abi.mjs` failed at HEAD (below); the flashes used the
  real gate with every check passing.
- **MSV4 gate fix (needed to flash).** `0b97b6b` deliberately (a) added a report-only
  `measureCp(g_bootOpenCp)` under `#ifdef MSV4_REPORT_BOOT_OPEN_CP` and (b) changed the mask-check
  counter from wakes to minutes, without updating `verify_msv4_packet_abi.mjs`. The check now
  asserts one *calibrating* `measureCp()` caller with those blocks removed, exactly one other
  (report-only) caller, and that it writes only `g_bootOpenCp`; the mask assertion matches
  `g_minutesSinceMaskCheck = MASK_CHECK_MINUTES`. Negative-tested: an extra calibrating caller,
  or the boot call writing `g_specCp`, both still fail it.
- **Flashed** receiver #1 (COM16, `MSV2088A3104A7AC`) after the history outbox drained to zero,
  first `rename-pass2-v1`, then `rename-pass2-v2` (the Display Center rename); hash verified both
  times. After boot: `/device` reports `rename-pass2-v2`, a cloud upload succeeded (0 TLS failures,
  0 dropped, 0 journal errors), `/app` title and manifest read `Display Center`.
- **Deployed** Netlify production (`msv2-plants.netlify.app`) twice, ending on deploy
  `6afffe851b09950cb6f7c375`: live `index.html` title `Display Center`, loads `app.v20.js` (200),
  SW cache `msv2-cloud-app-v20`, manifest `Display Center`. **Deployed** the `plant-dashboard`
  Edge Function twice; the live manifest returns `Display Center`.
- `node` on all 12 DisplayCenter `verify_*.mjs`, `verify_plant_dashboard_qr.mjs` (against
  `app.v20.js`), `verify_history_downsampling.mjs`, `verify_supabase_migrations.mjs` (43
  contiguous); `scripts/check_portfolio_claims.py` (pins v20): all PASS.
- `py_compile` on the 8 edited Python scripts, plus `--help` on 5 of them showing the new text.
- A repo-wide search for remaining `MSV2` outside dated docs, `MSV2/`, migrations and fleet.json
  leaves only generation references, keep-list identifiers, the print-release filenames and the
  Tier 3 items.
- Not run: the Deno test suite for `plant-dashboard/index.ts` (no `deno` on this machine; the change
  is string literals only).
