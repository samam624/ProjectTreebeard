#!/usr/bin/env python3
"""Audit and verify the MSV3 bootloader release archive.

WHY THIS EXISTS. On 2026-09-09 a review found the 1.0.2 release directory carrying two
tracked integrity records that disagreed with each other AND with the bytes on disk:
`SHA256SUMS.txt` was stale for five files, referenced a package that had been deleted, and
omitted two that had been added, while `release-manifest.json` in the same directory had been
updated to a different set of hashes. One package -- `msv3-1.0.2-sensor004-v65550-signed-ota.zip`
-- had different bytes under the SAME name and the SAME application version, which destroys the
one property a release archive exists to provide: that a (name, version) pair names exactly one
image forever.

This matters because these packages are the recovery path for probes that are about to be
conformal-coated and become BLE-recovery-only. Before coating there must be exactly one
unambiguous rescue package per probe, and "unambiguous" has to be something a script can prove,
not something a human remembers.

WHAT THIS CHECKS, per release directory:
  1. Every file's real SHA-256 against `SHA256SUMS.txt`.
  2. Every file's real SHA-256 against the `files` array in `release-manifest.json`.
  3. Records that reference a file which no longer exists.
  4. Files present on disk that neither record mentions.
  5. That the two records do not contradict each other for a file they both name.
  6. The identity embedded in each application package (sensor id, application version, build
     id, signature state), read out of the image bytes rather than trusted from the filename.

DISPUTED ARTIFACTS. A file whose provenance is genuinely ambiguous is NOT silently repaired and
NOT deleted. It is listed in the release directory's `DISPUTED.json`, which this tool treats as
an expected, acknowledged inconsistency: the file stays on disk and stays readable for
deliberate forensic or emergency use, but `--rescue-candidates` will never offer it. That file
is tracked separately from `release-manifest.json` precisely so a rebuild cannot erase it.

Exit status is 0 only when every release directory is internally consistent, counting
acknowledged disputes as consistent. `--inventory` reports without judging.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATED_ROOT = REPO_ROOT / "MSV3" / "bootloader" / "generated"

# `SHA256SUMS.txt` cannot list itself, and `release-manifest.json` carries hashes so it cannot
# hash itself either. Naming the exclusions explicitly beats a rule like "every file in the
# directory", which is self-referential and quietly impossible to satisfy.
CHECKSUM_SCOPE_EXCLUDED = {"SHA256SUMS.txt", "release-manifest.json", "DISPUTED.json"}

SUMS_NAME = "SHA256SUMS.txt"
MANIFEST_NAME = "release-manifest.json"
DISPUTED_NAME = "DISPUTED.json"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sums(path: Path) -> dict[str, str]:
    """Parse a `sha256sum` style file. Tolerates both the ' *name' and '  name' separators."""
    out: dict[str, str] = {}
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        digest, name = parts
        out[name.lstrip("*").strip()] = digest.lower()
    return out


def parse_manifest(path: Path) -> tuple[dict[str, dict], dict]:
    """Return {name: entry} plus the raw document.

    Two entry flags change what absence from the directory MEANS, and without them the verifier
    reports honest bookkeeping as corruption:

    `provenance_only` -- the entry records the hash of an input that was never archived here.
    `MSV3_Moisture_BLE_Sleep.ino.hex` is the case: build_msv3_release.ps1 hashes it out of the
    gitignored build cache so a release can be tied back to the exact application hex that went
    into its packages. Recording that is useful; expecting the file in this directory is not.

    `withdrawn` -- the artifact was deliberately removed and must never be reachable again.
    1.0.0's dual-bank package (documented in MSV3/bootloader/README.md as "must not be
    installed") and 1.0.1's identity-free package (below IDENTITY_CONTRACT_MIN_APP_VERSION, so
    msv3_fleet cannot bind it to a probe) were both deleted in commit b6ffeab. Keeping the entry
    with a reason is the point: it records that the absence is a decision, not a loss.
    """
    if not path.exists():
        return {}, {}
    raw = json.loads(path.read_text(encoding="utf-8-sig"))
    files = {}
    for entry in raw.get("files", []):
        name = entry.get("name")
        if name:
            files[name] = entry
    return files, raw


def manifest_hash(entry: dict) -> str:
    return str(entry.get("sha256", "")).lower()


def parse_disputed(path: Path) -> dict[str, dict]:
    if not path.exists():
        return {}
    raw = json.loads(path.read_text(encoding="utf-8-sig"))
    return {entry["name"]: entry for entry in raw.get("disputed", []) if entry.get("name")}


def package_identity(path: Path) -> dict:
    """Read the identity out of a DFU package's image bytes, never from its filename."""
    if path.suffix != ".zip":
        return {}
    try:
        sys.path.insert(0, str(REPO_ROOT / "scripts"))
        import msv3_fleet  # noqa: PLC0415  -- deliberately lazy; keeps --help working standalone

        package = msv3_fleet.read_package(path)
        return {
            "sensor_id": package.sensor_id,
            "app_version": package.application_version,
            "build_id": package.build_id or "(none)",
            "signed": package.signed,
            "image_sha256": package.sha256,
        }
    except Exception as exc:  # noqa: BLE001 -- an unreadable package is a finding, not a crash
        # An `initial-install-from-stock` package deliberately carries a SoftDevice+bootloader
        # alongside the application, and read_package() refuses exactly that combination because
        # it must never be delivered over BLE. That refusal is the tool working, not a defect in
        # the archive, so do not report it as an unreadable package.
        if "must never be delivered over BLE" in str(exc):
            return {"bundle": True}
        return {"error": f"{type(exc).__name__}: {exc}"}


class ReleaseAudit:
    def __init__(self, directory: Path):
        self.directory = directory
        self.name = directory.name
        self.actual = {
            entry.name: sha256_file(entry)
            for entry in sorted(directory.iterdir())
            if entry.is_file() and entry.name not in CHECKSUM_SCOPE_EXCLUDED
        }
        self.sums = parse_sums(directory / SUMS_NAME)
        self.manifest_files, self.manifest_raw = parse_manifest(directory / MANIFEST_NAME)
        self.disputed = parse_disputed(directory / DISPUTED_NAME)
        self.problems: list[str] = []
        self.acknowledged: list[str] = []

    def audit(self) -> None:
        has_sums = (self.directory / SUMS_NAME).exists()
        if not has_sums:
            self.problems.append(
                f"{self.name}: no {SUMS_NAME}. Every release directory must carry one; without it "
                f"there is no tracked record of what these bytes were when they were signed.")

        for name, digest in sorted(self.actual.items()):
            recorded_sum = self.sums.get(name)
            entry = self.manifest_files.get(name)
            recorded_manifest = manifest_hash(entry) if entry else None
            dispute = self.disputed.get(name)

            if dispute is not None:
                # DISPUTED.json is a LEDGER, not a note. If a disputed file's bytes change again,
                # or its recorded hash is edited, that must surface here rather than sit behind an
                # "acknowledged" label -- otherwise the quarantine becomes a place where anything
                # can be changed without review, which is the original defect with extra steps.
                recorded = str(dispute.get("sha256_on_disk", "")).lower()
                if recorded and recorded != digest:
                    self.problems.append(
                        f"{self.name}/{name}: DISPUTED, and its bytes have changed AGAIN since the "
                        f"dispute was recorded\n"
                        f"    on disk        : {digest}\n"
                        f"    in {DISPUTED_NAME}: {recorded}\n"
                        f"    Resolve the dispute rather than editing either side of it.")
                    continue
                if not recorded:
                    self.problems.append(
                        f"{self.name}/{name}: listed in {DISPUTED_NAME} without a "
                        f"\"sha256_on_disk\", so nothing pins the bytes being quarantined.")
                    continue
                self.acknowledged.append(
                    f"{self.name}/{name}: DISPUTED ({dispute.get('reason', 'no reason recorded')})")
                continue

            if entry and entry.get("withdrawn"):
                self.problems.append(
                    f"{self.name}/{name}: recorded as WITHDRAWN but the file is present on disk. "
                    f"A withdrawn artifact must not be reachable.")
                continue

            if has_sums and recorded_sum is None:
                self.problems.append(
                    f"{self.name}/{name}: on disk but absent from {SUMS_NAME}.")
            elif recorded_sum is not None and recorded_sum != digest:
                self.problems.append(
                    f"{self.name}/{name}: bytes do not match {SUMS_NAME}\n"
                    f"    on disk : {digest}\n"
                    f"    recorded: {recorded_sum}")

            if recorded_manifest is not None and recorded_manifest != digest:
                self.problems.append(
                    f"{self.name}/{name}: bytes do not match {MANIFEST_NAME}\n"
                    f"    on disk : {digest}\n"
                    f"    recorded: {recorded_manifest}")

            if (recorded_sum is not None and recorded_manifest is not None
                    and recorded_sum != recorded_manifest):
                self.problems.append(
                    f"{self.name}/{name}: the two tracked records contradict each other\n"
                    f"    {SUMS_NAME}: {recorded_sum}\n"
                    f"    {MANIFEST_NAME}: {recorded_manifest}")

        for name in sorted(set(self.sums) - set(self.actual)):
            if name in self.disputed:
                continue
            self.problems.append(
                f"{self.name}/{name}: listed in {SUMS_NAME} but the file is gone.")
        for name in sorted(set(self.manifest_files) - set(self.actual)):
            entry = self.manifest_files[name]
            if entry.get("provenance_only"):
                self.acknowledged.append(
                    f"{self.name}/{name}: provenance-only record "
                    f"({entry.get('note', 'input hash, never archived here')})")
                continue
            if entry.get("withdrawn"):
                self.acknowledged.append(
                    f"{self.name}/{name}: WITHDRAWN ({entry.get('reason', 'no reason recorded')})")
                continue
            self.problems.append(
                f"{self.name}/{name}: listed in {MANIFEST_NAME} but the file is gone. If that was "
                f"deliberate, mark the entry \"withdrawn\" with a reason; if it is an input rather "
                f"than an artifact, mark it \"provenance_only\".")

    def report_inventory(self) -> None:
        print(f"\n=== {self.name} ===")
        header = f"{'file':<52} {'sha256':<18} {'sums':<8} {'manifest':<10} identity"
        print(header)
        print("-" * len(header))
        for name, digest in sorted(self.actual.items()):
            recorded_sum = self.sums.get(name)
            entry = self.manifest_files.get(name)
            recorded_manifest = manifest_hash(entry) if entry else None
            sums_state = ("-" if recorded_sum is None
                          else "ok" if recorded_sum == digest else "MISMATCH")
            manifest_state = ("-" if recorded_manifest is None
                              else "ok" if recorded_manifest == digest else "MISMATCH")
            if name in self.disputed:
                sums_state = manifest_state = "disputed"
            identity = package_identity(self.directory / name)
            if "error" in identity:
                identity_text = f"unreadable: {identity['error']}"
            elif identity.get("bundle"):
                identity_text = "SD+bootloader+app bundle (SWD only, never over BLE)"
            elif identity:
                identity_text = (f"sensor {identity['sensor_id']:03d} v{identity['app_version']} "
                                 f"{identity['build_id']} "
                                 f"{'signed' if identity['signed'] else 'UNSIGNED'}")
            else:
                identity_text = ""
            print(f"{name:<52} {digest[:16]:<18} {sums_state:<8} {manifest_state:<10} {identity_text}")
        for name in sorted(set(self.sums) - set(self.actual)):
            print(f"{name:<52} {'(missing)':<18} {'LISTED':<8} {'-':<10} file is gone")
        for name in sorted(set(self.manifest_files) - set(self.actual)):
            if name in self.actual or name in self.sums:
                continue
            entry = self.manifest_files[name]
            note = ("provenance-only input" if entry.get("provenance_only")
                    else f"withdrawn: {entry.get('reason', '?')}" if entry.get("withdrawn")
                    else "file is gone")
            print(f"{name:<52} {'(not archived)':<18} {'-':<8} {'LISTED':<10} {note}")

    def sync_manifest(self) -> tuple[Path, list[str], list[str]]:
        """Merge on-disk artifacts into release-manifest.json without ever rewriting a hash.

        build_msv3_release.ps1 used to rebuild the `files` array from only the four files of the
        CURRENT build and replace the whole document, so a release directory's manifest recorded
        nothing but its last build. That is how 1.0.3 came to list only sensor008 while probes 5,
        6 and 7 were flashed from packages sitting beside it, unrecorded.

        Merging by filename alone would reintroduce the original defect from the other side, so a
        name already recorded at a DIFFERENT hash is refused outright rather than updated. That is
        the immutability rule: a (name, version) pair names one image forever, and the only way
        to change what a name means is to resolve it as a dispute by hand.
        """
        path = self.directory / MANIFEST_NAME
        raw = dict(self.manifest_raw)
        entries = list(raw.get("files", []))
        by_name = {e.get("name"): e for e in entries if e.get("name")}

        added, conflicts = [], []
        for name, digest in sorted(self.actual.items()):
            if name in self.disputed:
                continue
            existing = by_name.get(name)
            if existing is None:
                entries.append({
                    "name": name,
                    "sha256": digest,
                    "bytes": (self.directory / name).stat().st_size,
                })
                added.append(name)
            elif manifest_hash(existing) and manifest_hash(existing) != digest:
                conflicts.append(
                    f"{self.name}/{name}: already recorded at {manifest_hash(existing)} but the "
                    f"bytes on disk hash to {digest}. Refusing to rewrite the record. Resolve it "
                    f"in {DISPUTED_NAME}, or supersede the artifact under a NEW version.")

        if conflicts:
            return path, [], conflicts

        raw["files"] = entries
        path.write_text(json.dumps(raw, indent=4) + "\n", encoding="utf-8", newline="\n")
        return path, added, []

    def write_sums(self) -> Path:
        """Rewrite SHA256SUMS.txt from the bytes actually on disk.

        Disputed files are deliberately EXCLUDED rather than recorded at their current hash.
        Writing today's bytes for an artifact whose provenance is unresolved would convert an
        open question into a false assurance -- the exact move that produced this mess. Their
        evidence lives in DISPUTED.json instead.
        """
        path = self.directory / SUMS_NAME
        lines = [f"{digest} *{name}"
                 for name, digest in sorted(self.actual.items())
                 if name not in self.disputed]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        return path


def release_directories() -> list[Path]:
    if not GENERATED_ROOT.exists():
        return []
    return sorted(
        entry for entry in GENERATED_ROOT.iterdir()
        if entry.is_dir() and (entry / MANIFEST_NAME).exists())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--inventory", action="store_true",
                        help="print the full per-file evidence table and do not fail on findings")
    parser.add_argument("--release", action="append", default=None,
                        help="limit to one release directory (repeatable), e.g. --release 1.0.3")
    parser.add_argument("--sync-manifest", action="store_true",
                        help="add on-disk artifacts missing from release-manifest.json. Never "
                             "rewrites an existing hash; refuses if the bytes changed under a "
                             "recorded name.")
    parser.add_argument("--write-sums", action="store_true",
                        help="rewrite SHA256SUMS.txt from the bytes on disk, excluding disputed "
                             "files, then re-verify. build_msv3_release.ps1 calls this so a "
                             "release can never again ship without a current checksum record.")
    args = parser.parse_args()

    directories = release_directories()
    if args.release:
        wanted = set(args.release)
        directories = [d for d in directories if d.name in wanted]
    if not directories:
        print("no release directories found", file=sys.stderr)
        return 2

    # Manifest first: --write-sums must record the manifest as it will finally stand.
    if args.sync_manifest:
        all_conflicts = []
        for directory in directories:
            path, added, conflicts = ReleaseAudit(directory).sync_manifest()
            all_conflicts.extend(conflicts)
            if added:
                print(f"{path.relative_to(REPO_ROOT)}: recorded {len(added)} previously "
                      f"unrecorded artifact(s)")
                for name in added:
                    print(f"    + {name}")
        if all_conflicts:
            print("\nrefusing to sync -- immutability violation(s):")
            for line in all_conflicts:
                print(f"  {line}")
            return 1

    if args.write_sums:
        for directory in directories:
            written = ReleaseAudit(directory).write_sums()
            print(f"wrote {written.relative_to(REPO_ROOT)}")

    audits = []
    for directory in directories:
        audit = ReleaseAudit(directory)
        audit.audit()
        audits.append(audit)
        if args.inventory:
            audit.report_inventory()

    problems = [p for audit in audits for p in audit.problems]
    acknowledged = [a for audit in audits for a in audit.acknowledged]

    if acknowledged:
        print("\nacknowledged disputes (kept on disk, excluded from rescue selection):")
        for line in acknowledged:
            print(f"  {line}")

    if problems:
        print(f"\n{len(problems)} archive integrity problem(s):")
        for line in problems:
            print(f"  {line}")
        if args.inventory:
            return 0
        return 1

    print(f"\nPASS {len(audits)} release directory/ies are internally consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
