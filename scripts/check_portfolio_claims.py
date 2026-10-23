"""Fast, dependency-free checks for the public portfolio surface.

These checks do not claim that hardware was validated. They keep the recruiter-facing files,
versioned dashboard assets and contribution boundary internally consistent.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def main() -> None:
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    contributions = (ROOT / "CONTRIBUTIONS.md").read_text(encoding="utf-8")
    start_here = (ROOT / "docs" / "portfolio" / "START_HERE.md").read_text(encoding="utf-8")
    dashboard_html = (ROOT / "cloud" / "web" / "plant-dashboard" / "index.html").read_text(encoding="utf-8")
    service_worker = (ROOT / "cloud" / "web" / "plant-dashboard" / "sw.js").read_text(encoding="utf-8")

    required_paths = [
        "MSV4/hardware/MSV4_Probe_PCB/README.md",
        "MSV4/docs/MSV4_BOARD1_BRINGUP_2026-10-01.md",
        "MSV3/docs/MSV3_FOUR_BOARD_MUX_TRIAGE_2026-08-31.md",
        "MSV3/docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md",
        "MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md",
        "MSV4/docs/MSV4_LOCKIN_MEASUREMENT_NOTES_2026-10-01.md",
        "MSV4/docs/MSV4_MOISTURE_SLOPE_AND_THERMAL_FIT_2026-10-17.md",
    ]
    for relative in required_paths:
        require((ROOT / relative).is_file(), f"missing evidence file: {relative}")

    require("**Sam's role:**" in readme, "README must state Sam's scoped role near the top")
    require("AI-assisted" in readme and "AI-assisted" in contributions, "AI contribution boundary must remain explicit")
    require("synthetic-data dashboard demo" in readme, "README must label the dashboard demo as synthetic")
    require("not an accuracy" in start_here and "specification" in start_here, "reference experiment must retain its limitation")

    forbidden = {
        "115–122 sensor reads": "agent-solved logger story must not be a recruiter highlight",
        "Twenty bootloader builds": "agent-solved bootloader story must not be a recruiter highlight",
        "watching sun recharge it": "solar evidence must be described as a window-light test",
        "best candidate, a derived soil capacitance": "reference experiment must not claim a winner",
    }
    for phrase, reason in forbidden.items():
        require(phrase not in readme, reason)

    require('src="assets/app.v18.js"' in dashboard_html, "HTML must load dashboard app v18")
    require('"msv2-cloud-app-v18"' in service_worker, "service-worker cache must match dashboard app v18")
    require("representative synthetic data, not live measurements" in dashboard_html, "demo must disclose synthetic data")

    print("PASS portfolio claims, evidence paths and dashboard asset versions are consistent")


if __name__ == "__main__":
    main()
