#!/usr/bin/env python3
"""Slice the DisplayCenter V2 enclosure for a Bambu Lab P1S and package it.

Why this exists
---------------
The first v2 pack (2026-11-17) was sliced by pointing the Bambu Studio
02.09.01.70 command line at the system P1S preset. That preset pulls its
start, end, layer-change, timelapse and filament-change G-code in through an
``include`` list of template presets, which the command line does not resolve.
Every plate came out with the generic fallback start G-code
(``G28`` / ``G1 Z5``): no bed levelling, no nozzle wipe, no purge line, and no
textured-plate Z offset.

This script never asks the slicer to resolve anything. It flattens each
preset itself (``inherits`` chain + ``include`` list + the repo overlay in
``profiles/``) into a self-contained JSON, slices with those, and then checks
the G-code it got back against the flattened presets, key by key. The pack is
refused if the start G-code, or any other setting, is not what was asked for.

Output (default ``output/p1s_0p4_pla_<tag>/``, tag ``v3`` = prototype 10;
the pack folder must already hold the four ``*_print.stl`` files):
  * one ``.gcode.3mf`` per plate, ready to send from Bambu Studio / copy to
    the printer's microSD card
  * ``sliced_release_report.json``
  * the flattened presets used, in ``profiles/p1s_flattened/``

Run from anywhere:
  python tools/slice_p1s_pack.py
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from datetime import date
from pathlib import Path

V2 = Path(__file__).resolve().parents[1]
PROFILES = V2 / "profiles"
DEFAULT_BAMBU = Path(r"C:\Program Files\Bambu Studio")

MACHINE = "Bambu Lab P1S 0.4 nozzle"
PRINTER_MODEL = "Bambu Lab P1S"
BASE_PROCESS = "MSV2_DisplayCenter_P1S_0p4_PLA_process.json"
SUPPORT_PROCESS = "MSV2_DisplayCenter_P1S_0p4_PLA_rear_support_process.json"
FILAMENT = "MSV2_DisplayCenter_P1S_0p4_PLA_filament.json"

STEM = "MSV2_DisplayCenter_P1S_0p4_PLA"
FRONT = "MSV3_display_front_cover_print.stl"
REAR = "MSV3_display_rear_shell_print.stl"
STAND = "MSV3_display_kickstand_print.stl"
FIT = "MSV3_display_fit_check_plate_print.stl"
# (plate, STLs, process, supports, STLs whose supports are switched off per object)
# The split plates are in print order (fit-check first: see PRINT_IN_BAMBU_STUDIO.md).
# full_enclosure is the one-plate build, matching the MK4S pack's.
PLATES = [
    ("fit_check_plate", [FIT], BASE_PROCESS, False, []),
    ("front_and_stand", [FRONT, STAND], BASE_PROCESS, False, []),
    ("rear_shell", [REAR], SUPPORT_PROCESS, True, []),
    ("full_enclosure", [FRONT, REAR, STAND], SUPPORT_PROCESS, True, [FRONT, STAND]),
]

GCODE_KEYS = ("machine_start_gcode", "machine_end_gcode", "layer_change_gcode",
              "time_lapse_gcode", "change_filament_gcode")
META_KEYS = {"inherits", "include", "instantiation", "from", "setting_id", "name",
             "type", "description", "renamed_from", "version", "base_id"}
# Written by the slicer from things other than the three presets, so a
# difference here is not a preset that failed to load.
NOT_COMPARED = {"compatible_printers", "compatible_printers_condition",
                "compatible_prints", "compatible_prints_condition", "filament_id",
                "print_settings_id", "printer_settings_id", "filament_settings_id",
                "inherits_group", "default_print_profile", "default_filament_profile",
                "upward_compatible_machine", "print_compatible_printers",
                "different_settings_to_system", "filament_vendor"}


# --------------------------------------------------------------------------- presets
class PresetLibrary:
    """System presets of one vendor, indexed by name per category."""

    def __init__(self, vendor_dir: Path):
        self.index: dict[str, dict[str, Path]] = {}
        for cat in ("machine", "process", "filament"):
            idx = {}
            for p in (vendor_dir / cat).rglob("*.json"):
                try:
                    name = json.loads(p.read_text(encoding="utf-8")).get("name")
                except (json.JSONDecodeError, UnicodeDecodeError):
                    continue
                if name:
                    idx.setdefault(name, p)
            self.index[cat] = idx

    def flatten(self, cat: str, name: str, chain: list[str]) -> dict:
        path = self.index[cat].get(name)
        if path is None:
            raise SystemExit(f"system {cat} preset not found: {name!r}")
        chain.append(f"{name} ({path.name})")
        own = json.loads(path.read_text(encoding="utf-8"))
        out = self.flatten(cat, own["inherits"], chain) if own.get("inherits") else {}
        for inc in own.get("include", []):
            out.update({k: v for k, v in self.flatten(cat, inc, chain).items()
                        if k not in META_KEYS})
        out.update({k: v for k, v in own.items() if k not in ("inherits", "include")})
        return out


def flatten_repo(lib: PresetLibrary, cat: str, fname: str, chain: list[str]) -> dict:
    """A repo overlay on top of its parent, which may itself be a repo overlay."""
    own = json.loads((PROFILES / fname).read_text(encoding="utf-8"))
    chain.append(f"{own['name']} (profiles/{fname})")
    parent = own.get("inherits", "")
    repo_parent = next((p.name for p in PROFILES.glob("*.json")
                        if json.loads(p.read_text(encoding="utf-8")).get("name") == parent), None)
    base = (flatten_repo(lib, cat, repo_parent, chain) if repo_parent
            else lib.flatten(cat, parent, chain))
    base.update({k: v for k, v in own.items() if k not in ("inherits", "include")})
    return base


def system_parent(chain: list[str]) -> str:
    """The first system preset in a flatten chain (repo overlays are tagged profiles/)."""
    return next(c.rsplit(" (", 1)[0] for c in chain if "(profiles/" not in c)


def finalise(flat: dict, cat: str, name: str, parent: str) -> dict:
    """A user preset that carries EVERY key itself.

    ``inherits`` names the system parent only because the command line checks
    printer/process compatibility through it; with every key present, nothing
    is taken from that parent. The G-code check after slicing proves it.
    """
    out = copy.deepcopy(flat)
    for k in ("include", "setting_id", "base_id", "renamed_from", "description"):
        out.pop(k, None)
    out.update({"type": cat, "name": name, "inherits": parent, "from": "User",
                "instantiation": "true"})
    return out


# --------------------------------------------------------------------------- G-code checks
def config_block(gcode: str) -> dict[str, str]:
    m = re.search(r"; CONFIG_BLOCK_START\n(.*?); CONFIG_BLOCK_END", gcode, re.S)
    if not m:
        raise SystemExit("G-code has no CONFIG_BLOCK")
    out = {}
    for line in m.group(1).splitlines():
        if line.startswith("; ") and " = " in line:
            k, v = line[2:].split(" = ", 1)
            out[k] = v
    return out


def as_gcode_value(v) -> str:
    """How the slicer prints a preset value in the G-code config block."""
    if isinstance(v, list):
        return ",".join(as_gcode_value(x) for x in v)
    return str(v).replace("\n", "\\n")


def num_or_str(s: str):
    s = s.strip().rstrip("%")
    try:
        return float(s)
    except ValueError:
        return s


def gcode_unescape(s: str) -> str:
    """Undo the config block's C-style escaping (and quoting of string arrays)."""
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        s = s[1:-1]
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append({"n": "\n", "r": "\r", "t": "\t"}.get(s[i + 1], s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def _elements_equal(wa: list[str], ga: list[str]) -> bool:
    if len(wa) != len(ga):
        return False
    for a, b in zip(wa, ga):
        a, b = num_or_str(a), num_or_str(b)
        if isinstance(a, float) and isinstance(b, float):
            if not math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-9):
                return False
        elif str(a).strip() != str(b).strip():
            return False
    return True


def compare(want, got: str) -> str:
    """'equal', 'variant' (array resolved to its first, Direct Drive Standard,
    entry), or 'differ'."""
    if isinstance(want, list):
        wa = [str(x) for x in want]
    elif isinstance(want, str) and re.fullmatch(r"-?[\d.]+x-?[\d.]+", want):
        wa = want.split("x")  # a single point prints as "x,y"
    else:
        wa = [str(want)]
    g = gcode_unescape(got)
    if ",".join(wa) == g or (len(wa) == 1 and wa[0] == g):
        return "equal"
    ga = g.split(",")
    if _elements_equal(wa, ga):
        return "equal"
    # Arrays whose entries are all one value: the slicer pads them out to the
    # extruder count, or keeps only the entries for the variant in use.
    if len(set(wa)) == 1 and len(set(ga)) == 1 and _elements_equal(wa[:1], ga[:1]):
        return "equal"
    if len(wa) > 1 and _elements_equal(wa[:1], [g]):
        return "variant"
    return "differ"


def extrusion_extents(gcode: str):
    """Min/max of extruding moves after the first layer change (absolute XYZ)."""
    body = gcode.split("; CHANGE_LAYER", 1)[1]
    x = y = z = None
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    for line in body.splitlines():
        if not (line.startswith("G1 ") or line.startswith("G0 ") or
                line.startswith("G2 ") or line.startswith("G3 ")):
            continue
        for ax, val in re.findall(r"([XYZE])(-?[\d.]+)", line.split(";", 1)[0]):
            if ax == "X": x = float(val)
            elif ax == "Y": y = float(val)
            elif ax == "Z": z = float(val)
        e = re.search(r"\bE(-?[\d.]+)", line.split(";", 1)[0])
        if e and float(e.group(1)) > 0 and None not in (x, y, z):
            for i, v in enumerate((x, y, z)):
                lo[i] = min(lo[i], v)
                hi[i] = max(hi[i], v)
    return lo, hi


def first_layer_seconds(gcode: str, total_s: float) -> float:
    """Seconds spent before layer 2 starts, from the M73 remaining-time markers."""
    parts = gcode.split("; CHANGE_LAYER")
    if len(parts) < 3:
        return total_s
    head = "; CHANGE_LAYER".join(parts[:2])
    rem = re.findall(r"^M73 P\d+ R(\d+)", head, re.M)
    return max(0.0, total_s - 60.0 * int(rem[-1])) if rem else total_s


# --------------------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--bambu", type=Path, default=DEFAULT_BAMBU,
                    help="Bambu Studio install dir (default: %(default)s)")
    ap.add_argument("--tag", default="v3",
                    help="pack tag in the folder and file names (v2 = prototype 9)")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--release", default=None)
    ap.add_argument("--design-version", default="prototype-10-corner-standoff-chamfer")
    ap.add_argument("--keep-work", action="store_true")
    args = ap.parse_args()
    if args.out is None:
        args.out = V2 / "output" / f"p1s_0p4_pla_{args.tag}"
    if args.release is None:
        args.release = f"{STEM}_{args.tag}"

    exe = args.bambu / "bambu-studio.exe"
    vendor = args.bambu / "resources" / "profiles" / "BBL"
    lib = PresetLibrary(vendor)
    model_id = json.loads((vendor / "machine" / f"{PRINTER_MODEL}.json")
                          .read_text(encoding="utf-8"))["model_id"]
    version = json.loads((args.bambu / "resources" / "profiles" / "BBL.json")
                         .read_text(encoding="utf-8")).get("version", "?")

    # Flatten the three presets.
    chains: dict[str, list[str]] = {}
    chains["machine"] = []
    machine = finalise(lib.flatten("machine", MACHINE, chains["machine"]), "machine",
                       f"MSV2 DisplayCenter {MACHINE} (flattened)", MACHINE)
    for k in GCODE_KEYS:
        if not machine.get(k) or "G28 ; home all axes\nG1 Z5 F5000" in machine[k][:60]:
            raise SystemExit(f"flattened machine preset has no real {k}")
    chains["filament"] = []
    fil_raw = flatten_repo(lib, "filament", FILAMENT, chains["filament"])
    filament = finalise(fil_raw, "filament", fil_raw["name"], system_parent(chains["filament"]))
    processes = {}
    for pf in (BASE_PROCESS, SUPPORT_PROCESS):
        chains[pf] = []
        raw = flatten_repo(lib, "process", pf, chains[pf])
        processes[pf] = finalise(raw, "process", raw["name"], system_parent(chains[pf]))

    for pf, proc in processes.items():
        stray = sorted(k for k in proc if k in machine and k not in META_KEYS
                       and k not in NOT_COMPARED and k != "curr_bed_type")
        if stray:
            raise SystemExit(f"{pf} sets machine settings {stray}; they belong to the "
                             "machine preset and would silently override it")

    flat_dir = PROFILES / "p1s_flattened"
    flat_dir.mkdir(exist_ok=True)
    flat_paths = {"machine": flat_dir / "machine_Bambu_Lab_P1S_0p4.json",
                  "filament": flat_dir / "filament_MSV2_Generic_PLA_P1S_0p4.json",
                  BASE_PROCESS: flat_dir / "process_MSV2_P1S_0p4_PLA.json",
                  SUPPORT_PROCESS: flat_dir / "process_MSV2_P1S_0p4_PLA_rear_shell.json"}
    for key, obj in (("machine", machine), ("filament", filament),
                     (BASE_PROCESS, processes[BASE_PROCESS]),
                     (SUPPORT_PROCESS, processes[SUPPORT_PROCESS])):
        flat_paths[key].write_text(json.dumps(obj, indent=4, ensure_ascii=False) + "\n",
                                   encoding="utf-8")

    work = Path(tempfile.mkdtemp(prefix="p1s_slice_"))
    report = {
        "release": args.release,
        "design_version": args.design_version,
        "generated": date.today().isoformat(),
        "slicer": f"Bambu Studio command line ({exe}), BBL profiles {version}",
        "printer": PRINTER_MODEL, "printer_model_id": model_id,
        "nozzle_diameter_mm": 0.4, "material": "Generic PLA",
        "plate": filament.get("curr_bed_type") or processes[BASE_PROCESS].get("curr_bed_type"),
        "preset_chains": chains,
        "flattened_profiles": {k: str(v.relative_to(V2)).replace("\\", "/")
                               for k, v in flat_paths.items()},
        "projects": {},
    }
    failures: list[str] = []

    def slice_once(inputs: list[Path], pdir: Path, out_name: str, pf: str, arrange: bool):
        pdir.mkdir(parents=True)
        cmd = [str(exe), "--debug", "2", "--slice", "0", "--arrange", "1" if arrange else "0",
               "--orient", "0",
               "--load-settings", f"{flat_paths['machine']};{flat_paths[pf]}",
               "--load-filaments", str(flat_paths["filament"]),
               "--outputdir", str(pdir), "--export-3mf", out_name, *map(str, inputs)]
        # On 2026-11-23 bambu-studio.exe wrote a complete project and a
        # "Success." result.json, then never exited, and Windows could not
        # kill it. So stop waiting once result.json reports success and the
        # project file has stopped growing; every later check still runs on
        # what it wrote.
        result_path = pdir / "result.json"
        produced = pdir / out_name
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 1800
        last_size, stable_since = -1, None
        while proc.poll() is None:
            if time.monotonic() > deadline:
                proc.kill()
                break
            time.sleep(2)
            try:
                done = json.loads(result_path.read_text(encoding="utf-8")).get("return_code") == 0
            except (OSError, ValueError):
                done = False
            size = produced.stat().st_size if produced.exists() else -1
            if size != last_size:
                last_size, stable_since = size, time.monotonic()
            if done and size > 0 and time.monotonic() - stable_since >= 20:
                print(f"  slicer reported success but did not exit; continuing", flush=True)
                proc.kill()
                break
        result = json.loads(result_path.read_text(encoding="utf-8")) if result_path.exists() else {}
        return result.get("return_code", proc.returncode), produced, result

    for plate, stls, pf, supports, no_support in PLATES:
        out_name = f"{STEM}_{plate}_{args.tag}.gcode.3mf"
        print(f"[{plate}] slicing ...", flush=True)
        rc, produced, result = slice_once([args.out / s for s in stls], work / plate / "pass1",
                                          out_name, pf, arrange=True)
        if rc == 0 and produced.exists() and no_support:
            # Pass 2: switch supports off per object on the arranged project, then
            # slice it again as placed.
            with zipfile.ZipFile(produced) as zin:
                ms = zin.read("Metadata/model_settings.config").decode("utf-8")
                for stl in no_support:
                    ms, n = re.subn(
                        rf'(<object id="\d+">\s*<metadata key="name" value="{re.escape(stl)}"/>)',
                        r'\1\n    <metadata key="enable_support" value="0"/>', ms)
                    if n != 1:
                        failures.append(f"{plate}: could not find object {stl} to disable supports")
                staged = work / plate / f"{plate}_per_object.3mf"
                with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED) as zout:
                    for info in zin.infolist():
                        if info.filename.startswith("Metadata/plate_1.gcode"):
                            continue  # drop pass-1 results; pass 2 re-slices
                        data = (ms.encode("utf-8") if info.filename == "Metadata/model_settings.config"
                                else zin.read(info.filename))
                        zout.writestr(info, data)
            rc, produced, result = slice_once([staged], work / plate / "pass2", out_name, pf,
                                              arrange=False)
        if rc != 0 or not produced.exists():
            failures.append(f"{plate}: slicer rc={rc} {result.get('error_string', '')}".strip())
            continue

        # ---- verify what came back
        with zipfile.ZipFile(produced) as z:
            names = z.namelist()
            gbytes = z.read("Metadata/plate_1.gcode")
            md5_file = z.read("Metadata/plate_1.gcode.md5").decode().strip()
            slice_info = z.read("Metadata/slice_info.config").decode("utf-8")
        gcode = gbytes.decode("utf-8")
        md5 = hashlib.md5(gbytes).hexdigest().upper()
        if md5 != md5_file.upper():
            failures.append(f"{plate}: embedded G-code md5 mismatch")
        cfg = config_block(gcode)

        mismatches, variants = [], []
        for src_name, src in (("machine", machine), ("process", processes[pf]),
                              ("filament", filament)):
            for k, v in src.items():
                if k in META_KEYS or k in NOT_COMPARED or k not in cfg:
                    continue
                verdict = compare(v, cfg[k])
                if verdict == "variant":
                    variants.append(k)
                elif verdict == "differ":
                    mismatches.append({"preset": src_name, "key": k,
                                       "expected": as_gcode_value(v)[:160],
                                       "gcode": cfg[k][:160]})
        gcode_bad = [k for k in GCODE_KEYS if compare(machine[k], cfg.get(k, "")) != "equal"]
        # Loading a single-filament project (pass 2) switches the prime tower off.
        # Harmless only if no tower was generated, so check the G-code for one.
        benign = [m for m in mismatches if m["key"] == "enable_prime_tower"
                  and gcode_unescape(m["gcode"]) == "0"
                  and not re.search(r"^; FEATURE: (Prime|Wipe) tower", gcode, re.M)]
        mismatches = [m for m in mismatches if m not in benign]
        if gcode_bad:
            failures.append(f"{plate}: custom G-code not from the P1S preset: {sorted(set(gcode_bad))}")
        if mismatches:
            failures.append(f"{plate}: {len(mismatches)} setting(s) differ from the flattened presets")

        expect = {"printer_model": PRINTER_MODEL, "nozzle_diameter": "0.4",
                  "curr_bed_type": "Textured PEI Plate", "filament_type": "PLA",
                  "layer_height": "0.2", "enable_support": "1" if supports else "0"}
        for k, v in expect.items():
            if compare(v, cfg.get(k, "<missing>")) != "equal":
                failures.append(f"{plate}: {k} = {cfg.get(k)!r}, expected {v!r}")
        if ";===== machine: P1S" not in gcode.split("; CHANGE_LAYER", 1)[0]:
            failures.append(f"{plate}: P1S start G-code banner not found before layer 1")

        lo, hi = extrusion_extents(gcode)
        area = [tuple(map(float, p.split("x"))) for p in machine["printable_area"]]
        excl = [tuple(map(float, p.split("x"))) for p in machine.get("bed_exclude_area", [])]
        ax0, ay0 = min(p[0] for p in area), min(p[1] for p in area)
        ax1, ay1 = max(p[0] for p in area), max(p[1] for p in area)
        if lo[0] < ax0 or lo[1] < ay0 or hi[0] > ax1 or hi[1] > ay1 or \
                hi[2] > float(machine["printable_height"]):
            failures.append(f"{plate}: extrusion outside the printable volume {lo} {hi}")
        if excl:
            ex1, ey1 = max(p[0] for p in excl), max(p[1] for p in excl)
            if lo[0] < ex1 and lo[1] < ey1:
                # bounding-box test is conservative; look for an actual move inside
                pts = [(float(a), float(b)) for a, b in
                       re.findall(r"^G1 X([0-9.]+) Y([0-9.]+)[^\n;]*E[0-9.]",
                                  gcode.split("; CHANGE_LAYER", 1)[1], re.M)]
                if any(px < ex1 and py < ey1 for px, py in pts):
                    failures.append(f"{plate}: extrusion inside the bed exclude area")

        # ---- every part present, and support only where it was asked for
        objs = {m.group(1): re.split(r"[\\/]", m.group(2))[-1] for m in
                re.finditer(r'<object identify_id="(\d+)" name="([^"]*)"', slice_info)}
        if sorted(objs.values()) != sorted(stls):
            failures.append(f"{plate}: plate holds {sorted(objs.values())}, expected {sorted(stls)}")
        support_lines: dict[str, int] = {}
        cur = None
        for line in gcode.splitlines():
            if line.startswith("; start printing object, unique label id: "):
                cur = line.rsplit(" ", 1)[1]
            elif line.startswith("; stop printing object"):
                cur = None
            elif line.startswith("; FEATURE: Support"):
                who = objs.get(cur, f"label {cur}") if cur else "unlabelled"
                support_lines[who] = support_lines.get(who, 0) + 1
        total_support = sum(support_lines.values())
        if not supports and total_support:
            failures.append(f"{plate}: support generated on a no-support plate: {support_lines}")
        if supports and not total_support:
            failures.append(f"{plate}: supports enabled but none generated")
        if no_support:
            if support_lines.get("unlabelled"):
                failures.append(f"{plate}: support outside any object block, cannot attribute it")
            for stl in no_support:
                if support_lines.get(stl):
                    failures.append(f"{plate}: {stl} got support although it is switched off")

        # ---- slice_info: fill what the command line leaves blank, as the GUI would
        sec = float(re.search(r'key="prediction" value="([\d.]+)"', slice_info).group(1))
        grams = float(re.search(r'key="weight" value="([\d.]+)"', slice_info).group(1))
        patched = []
        new_info = slice_info
        if re.search(r'key="printer_model_id" value=""', new_info):
            new_info = new_info.replace('key="printer_model_id" value=""',
                                        f'key="printer_model_id" value="{model_id}"')
            patched.append(f"printer_model_id=''->{model_id}")
        flt = re.search(r'key="first_layer_time" value="([^"]*)"', new_info)
        # The command line leaves this uninitialised: garbage on one run, 0 on the next.
        if flt and (not flt.group(1) or not 0 < float(flt.group(1)) <= sec):
            fl = first_layer_seconds(gcode, sec)
            new_info = new_info.replace(flt.group(0), f'key="first_layer_time" value="{fl:.6f}"')
            patched.append(f"first_layer_time={flt.group(1)[:12]}...->{fl:.0f}s (from M73)")
        fid = filament.get("filament_id", "")
        if fid and 'tray_info_idx=""' in new_info:
            new_info = new_info.replace('tray_info_idx=""', f'tray_info_idx="{fid}"')
            patched.append(f"tray_info_idx=''->{fid}")
        # Object names carry the absolute path of the STL on this laptop.
        new_info = re.sub(r'name="[^"]*[\\/]([^"\\/]+)"', r'name="\1"', new_info)

        final = args.out / out_name
        with zipfile.ZipFile(produced) as zin, \
                zipfile.ZipFile(final, "w", zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                data = zin.read(info.filename)
                if info.filename == "Metadata/slice_info.config":
                    data = new_info.encode("utf-8")
                elif info.filename == "Metadata/model_settings.config":
                    data = re.sub(rb'value="[^"]*[\\/]([^"\\/]+\.stl)"', rb'value="\1"', data)
                zout.writestr(info, data)
        with zipfile.ZipFile(final) as z:  # re-read the file actually written
            if hashlib.md5(z.read("Metadata/plate_1.gcode")).hexdigest().upper() != md5:
                failures.append(f"{plate}: G-code changed while repackaging")

        report["projects"][plate] = {
            "file": out_name,
            "inputs": stls,
            "process": processes[pf]["name"],
            "return_code": rc,
            "estimated_seconds": sec,
            "estimated_filament_g": grams,
            "supports": ("tree(auto), build plate only"
                         + (f"; switched off per object for {no_support}" if no_support else "")
                         if supports else False),
            "support_feature_blocks_by_object": support_lines,
            "objects": len(stls),
            "gcode_md5": md5,
            "start_gcode": cfg["machine_start_gcode"].split("\\n", 2)[0],
            "extrusion_extents_mm": {"min": [round(v, 2) for v in lo],
                                     "max": [round(v, 2) for v in hi]},
            "settings_compared": sum(1 for s in (machine, processes[pf], filament)
                                     for k in s if k in cfg and k not in META_KEYS
                                     and k not in NOT_COMPARED),
            "setting_mismatches": mismatches,
            "resolved_to_standard_nozzle_variant": sorted(variants),
            "accepted_differences": [f"{m['key']}: {m['expected']} -> {m['gcode']} "
                                     "(single filament, no prime tower in the G-code)"
                                     for m in benign],
            "slice_info_patched": patched,
        }
        print(f"[{plate}] {sec/60:.0f} min, {grams:.1f} g, "
              f"{report['projects'][plate]['settings_compared']} settings checked, "
              f"{len(mismatches)} mismatches, patched: {patched or 'nothing'}", flush=True)

    split = ("front_and_stand", "rear_shell")
    tot_s = sum(report["projects"][k]["estimated_seconds"] for k in split if k in report["projects"])
    tot_g = sum(report["projects"][k]["estimated_filament_g"] for k in split if k in report["projects"])
    report["totals_enclosure_split_plates"] = {"estimated_seconds": tot_s,
                                                "estimated_filament_g": tot_g}
    report["failures"] = failures
    (args.out / "sliced_release_report.json").write_text(json.dumps(report, indent=2) + "\n",
                                                          encoding="utf-8")
    if args.keep_work:
        print(f"work dir kept: {work}")
    else:
        shutil.rmtree(work, ignore_errors=True)
    if failures:
        print("\nREFUSED:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print(f"\nOK: {len(report['projects'])} plates sliced and verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
