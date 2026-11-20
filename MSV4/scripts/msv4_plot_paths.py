"""Output location for recurring plots made from sensor history CSVs."""
from pathlib import Path


SENSOR_LOGS = Path(__file__).resolve().parents[2] / "data" / "sensor_logs"


def default_plot_dir(csv: Path) -> Path:
    """Keep project plots in latest/ and standalone plots alongside their input."""
    if SENSOR_LOGS in csv.resolve().parents:
        return SENSOR_LOGS / "latest"
    return csv.parent
