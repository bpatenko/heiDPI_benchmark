from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------

# Basisverzeichnis, in dem sich die Szenario‑Ordner befinden. Standardmäßig
# liegt das Skript im gleichen Verzeichnis wie diese Ordner.
BASEDIR = Path(__file__).resolve().parent

# Ordner mit Szenarien und optionale Beschriftungen. Wenn ein Label ``None``
# ist, wird zur Laufzeit automatisch aus der mittleren verarbeiteten
# Eventrate ein Label gebildet. Passen Sie die Namen ggf. an Ihre
# Verzeichnisstruktur an.
SCENARIOS: Dict[str, str | None] = {
    "../benchmark_results/burst_cpp_500events": None,
    "../benchmark_results/burst_cpp_1000events": None,  # Label wird automatisch bestimmt
    "../benchmark_results/burst_cpp_2000events": None,
    "../benchmark_results/burst_cpp_4000events": None,
}

# Speicherquelle: ``"logger"`` verwendet ``logger_memory`` (falls
# vorhanden), ``"total"`` verwendet ``total_memory``.
MEMORY_SOURCE = "logger"

# Ausgabeverzeichnis für die Plots
OUTPUT_DIR = BASEDIR / "plots_burstcpp"


# ---------------------------------------------------------------------------
# Hilfsfunktionen
# ---------------------------------------------------------------------------

def apply_plot_style(fig_width: float = 8, fig_height: float = 4, enable_grid: bool = True) -> None:
    """Setzt globale Matplotlib‑Parameter für eine einheitliche Darstellung."""
    plt.style.use("default")
    plt.rcParams["figure.figsize"] = (fig_width, fig_height)
    plt.rcParams["axes.titlesize"] = 12
    plt.rcParams["axes.labelsize"] = 11
    plt.rcParams["xtick.labelsize"] = 10
    plt.rcParams["ytick.labelsize"] = 10
    plt.rcParams["legend.fontsize"] = 10
    if enable_grid:
        plt.rcParams["axes.grid"] = True
        plt.rcParams["grid.alpha"] = 0.6


def finalize_plot(output_path: Path) -> None:
    """Speichert den aktuellen Plot und schließt ihn."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()


def parse_watch_file(filepath: Path, memory_source: str) -> Tuple[pd.DataFrame, pd.DataFrame]:
    """Parst eine einzelne .json.watch‑Datei und liefert zwei DataFrames.

    events_df hat die Spalten ``file``, ``gen_second``, ``done_second`` und
    ``latency_ms``.  resources_df hat die Spalten ``file``, ``second``,
    ``cpu`` und ``mem``.

    Der Zeitbezug (Sekunde 0) wird jeweils pro Datei auf den ersten
    ``generator_ts``/``watcher_ts`` bzw. ``timestamp`` normiert, um eine
    relative Zeitskala zu erhalten.
    """
    events: List[Dict[str, object]] = []
    resources: List[Dict[str, object]] = []
    fname = filepath.name
    with open(filepath) as f:
        first_gen_ts = None
        first_wch_ts = None
        first_res_ts = None
        for line in f:
            rec = json.loads(line)
            # Flow‑Event (enthält packet_id)
            if "packet_id" in rec:
                if first_gen_ts is None:
                    first_gen_ts = rec["generator_ts"]
                if first_wch_ts is None:
                    first_wch_ts = rec["watcher_ts"]
                # relative Zeiten in Sekunden
                gen_s = (rec["generator_ts"] - first_gen_ts) / 1e6
                done_s = (rec["watcher_ts"] - first_wch_ts) / 1e6
                latency_ms = (rec["watcher_ts"] - rec["generator_ts"]) / 1e3
                events.append({
                    "file": fname,
                    "gen_second": int(gen_s),
                    "done_second": int(done_s),
                    "latency_ms": latency_ms,
                })
            else:
                # Ressourcen‑Event
                if first_res_ts is None:
                    first_res_ts = rec["timestamp"]
                t_s = (rec["timestamp"] - first_res_ts) / 1e6
                total_cpu = rec.get("total_cpu")
                logger_mem = rec.get("logger_memory")
                total_mem = rec.get("total_memory")
                # Speicherwahl: kB -> MB umrechnen
                if memory_source == "logger" and logger_mem is not None:
                    mem_val = float(logger_mem) / 1024.0
                elif total_mem is not None:
                    mem_val = float(total_mem) / 1024.0
                else:
                    mem_val = np.nan
                resources.append({
                    "file": fname,
                    "second": int(t_s),
                    "cpu": float(total_cpu) if total_cpu is not None else np.nan,
                    "mem": mem_val,
                })
    events_df = pd.DataFrame(events)
    resources_df = pd.DataFrame(resources)
    return events_df, resources_df


def load_scenario(dir_path: Path, memory_source: str) -> Tuple[pd.DataFrame, pd.DataFrame]:
    """Lädt alle .json.watch‑Dateien eines Szenarios und kombiniert die Daten."""
    events_list: List[pd.DataFrame] = []
    resources_list: List[pd.DataFrame] = []
    if not dir_path.exists() or not dir_path.is_dir():
        raise FileNotFoundError(f"Szenario‑Ordner nicht gefunden: {dir_path}")
    for fname in sorted(os.listdir(dir_path)):
        if fname.endswith(".json.watch"):
            e_df, r_df = parse_watch_file(dir_path / fname, memory_source)
            if not e_df.empty:
                events_list.append(e_df)
            if not r_df.empty:
                resources_list.append(r_df)
    if not events_list:
        raise ValueError(f"Keine Events im Szenario {dir_path} gefunden.")
    events_df = pd.concat(events_list, ignore_index=True)
    resources_df = pd.concat(resources_list, ignore_index=True) if resources_list else pd.DataFrame(columns=["file", "second", "cpu", "mem"])
    return events_df, resources_df


def aggregate_metric_per_second(df: pd.DataFrame, value_col: str) -> pd.DataFrame:
    """Aggregiert eine Metrik pro Sekunde über alle Läufe eines Szenarios.

    Die Funktion berechnet zunächst für jeden Lauf den Mittelwert der Metrik pro
    Sekunde und aggregiert anschließend über alle Läufe hinweg die mittlere,
    minimale und maximale Ausprägung pro Sekunde.
    """
    if df.empty:
        return pd.DataFrame(columns=["second", "mean", "min", "max"])
    tmp = df.copy()
    tmp[value_col] = pd.to_numeric(tmp[value_col], errors="coerce")
    # Mittelwert pro Lauf und Sekunde
    per_run = tmp.groupby(["file", "second"])[value_col].mean().reset_index()
    # Aggregation über alle Läufe
    agg = (per_run.groupby("second")[value_col]
           .agg(mean="mean", min="min", max="max")
           .reset_index())
    return agg


def aggregate_processed_rate(events_df: pd.DataFrame) -> pd.DataFrame:
    """Berechnet den verarbeiteten Durchsatz (Events pro Sekunde) über alle Läufe.

    Erzeugt ein DataFrame mit den Spalten ``second``, ``mean_rate``,
    ``min_rate`` und ``max_rate``, welches den Mittelwert, das Minimum und das
    Maximum der pro Sekunde verarbeiteten Events über die Läufe enthält.
    """
    if events_df.empty:
        return pd.DataFrame(columns=["second", "mean_rate", "min_rate", "max_rate"])
    # Zähle verarbeitete Events pro Lauf und Sekunde
    done_counts = events_df.groupby(["file", "done_second"]).size().reset_index(name="count")
    done_counts = done_counts.rename(columns={"done_second": "second"})
    # Aggregation über alle Läufe
    agg = (done_counts.groupby("second")["count"]
           .agg(mean_rate="mean", min_rate="min", max_rate="max")
           .reset_index())
    return agg


def determine_label(done_agg: pd.DataFrame) -> str:
    """Erstellt ein Label auf Basis der mittleren verarbeiteten Eventrate.

    Falls ``done_agg`` leer ist, wird ``"unknown"`` zurückgegeben.
    """
    if done_agg.empty:
        return "unknown"
    mean_rate = done_agg["mean_rate"].mean()
    # Runde auf ganze Zahlen und formatiere
    return f"{int(round(mean_rate))} events/s"


def plot_metric(metric_data: Dict[str, pd.DataFrame], ylabel: str, title: str, output_file: Path) -> None:
    """Zeichnet einen Plot für eine Metrik (CPU, Durchsatz oder Speicher).

    ``metric_data`` ist ein Mapping von Labels zu DataFrames mit den Spalten
    ``second``, ``mean``, ``min`` und ``max`` (bei Durchsatz: ``mean_rate``,
    ``min_rate``, ``max_rate``). Der Funktionsname bleibt generisch, um
    verschiedene Metriken zu behandeln.
    """
    apply_plot_style(fig_width=8, fig_height=4, enable_grid=True)
    fig, ax = plt.subplots()
    for label, df in metric_data.items():
        if df.empty:
            continue
        # Unterscheidung zwischen CPU/Mem (mean, min, max) und Durchsatz (mean_rate ...)
        if "mean" in df.columns:
            x = df["second"]
            y = df["mean"]
            y_min = df["min"]
            y_max = df["max"]
        else:
            x = df["second"]
            y = df["mean_rate"]
            y_min = df["min_rate"]
            y_max = df["max_rate"]
        # Stelle sicher, dass numerische Typen vorliegen. Wir wandeln alle
        # Sequenzen explizit in ``float``‑Arrays um; nicht interpretierbare
        # Werte werden zu ``NaN`` (``numpy.nan``). Ohne diese Umwandlung kann
        # ``fill_between`` bei gemischten Datentypen mit einem TypeError
        # fehlschlagen (vgl. matplotlib/numpy Issue #21420).
        x_vals = pd.to_numeric(x, errors="coerce").astype(float).to_numpy()
        y_vals = pd.to_numeric(y, errors="coerce").astype(float).to_numpy()
        y_min_vals = pd.to_numeric(y_min, errors="coerce").astype(float).to_numpy()
        y_max_vals = pd.to_numeric(y_max, errors="coerce").astype(float).to_numpy()
        ax.plot(x_vals, y_vals, linewidth=1.5, label=label)
        # Transparenter Bereich für min–max ohne Label, damit die Legende
        # nicht überfüllt wird
        ax.fill_between(x_vals, y_min_vals, y_max_vals, alpha=0.2)
    ax.set_xlabel("Zeit (s)")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend(loc="upper right")
    finalize_plot(output_file)


def main() -> None:
    latency_data: Dict[str, pd.DataFrame] = {}
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    # Dictionaries zur Aufnahme der aggregierten Daten pro Szenario
    cpu_data: Dict[str, pd.DataFrame] = {}
    mem_data: Dict[str, pd.DataFrame] = {}
    throughput_data: Dict[str, pd.DataFrame] = {}

    for scenario_dir, custom_label in SCENARIOS.items():
        dir_path = BASEDIR / scenario_dir
        try:
            events_df, res_df = load_scenario(dir_path, MEMORY_SOURCE)
        except (FileNotFoundError, ValueError) as exc:
            # Szenario überspringen, wenn es nicht vorhanden ist
            print(f"Warnung: {exc}")
            continue
        # Aggregationen berechnen
        cpu_agg = aggregate_metric_per_second(res_df, "cpu")
        mem_agg = aggregate_metric_per_second(res_df, "mem")
        proc_agg = aggregate_processed_rate(events_df)
        # Latenz (ms) pro Sekunde aggregieren (über done_second)
        latency_df = (events_df[["file", "done_second", "latency_ms"]]
                      .rename(columns={"done_second": "second"}))
        lat_agg = aggregate_metric_per_second(latency_df, "latency_ms")
        # Label bestimmen: entweder benutzerdefiniert oder automatisch
        label: str
        if custom_label:
            label = custom_label
        else:
            label = determine_label(proc_agg)
        cpu_data[label] = cpu_agg
        mem_data[label] = mem_agg
        throughput_data[label] = proc_agg
        latency_data[label] = lat_agg

    # CPU‑Plot
    if cpu_data:
        plot_metric(cpu_data, ylabel="CPU‑Auslastung (%)", title="CPU‑Auslastung über die Zeit", output_file=OUTPUT_DIR / "cpu_usage_burstcpp_combined.png")
    # Durchsatz‑Plot
    if throughput_data:
        plot_metric(throughput_data, ylabel="Events/s (verarbeitet)", title="Verarbeiteter Durchsatz über die Zeit", output_file=OUTPUT_DIR / "throughput_burstcpp.png")
    # Speicher‑Plot
    if mem_data:
        plot_metric(mem_data, ylabel="Speicher (MB)", title="Speicherbelegung über die Zeit", output_file=OUTPUT_DIR / "memory_usage_burstcpp_combined.png")
    # Latenz-Plot
    if latency_data:
        plot_metric(
            latency_data,
            ylabel="Latenz (ms)",
            title="Durchschnittliche Latenz über die Zeit",
            output_file=OUTPUT_DIR / "latency_burstcpp.png",
        )


if __name__ == "__main__":
    main()