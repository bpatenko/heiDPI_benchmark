
import os
import json
from pathlib import Path
from typing import List, Tuple

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

def apply_plot_style(fig_width=10, fig_height=5, enable_grid=True, tight=True):
    try:
        plt.style.use("default")
    except Exception:
        pass
    plt.rcParams["figure.figsize"] = (fig_width, fig_height)
    plt.rcParams["axes.titlesize"] = 12
    plt.rcParams["axes.labelsize"] = 11
    plt.rcParams["xtick.labelsize"] = 10
    plt.rcParams["ytick.labelsize"] = 10
    plt.rcParams["legend.fontsize"] = 10
    if enable_grid:
        plt.rcParams["axes.grid"] = True
        plt.rcParams["grid.alpha"] = 0.6
def finalize_plot(output_path: Path, tight=True, dpi=150):
    if tight:
        plt.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_path, dpi=dpi)
    plt.close()

# ===== Fixed paths (no CLI arguments) =====
BASEDIR = Path("../benchmark_results/burst_cpp_4000events")         # input directory with .json.watch files
OUTDIR  = Path("plots_cpp_combined")           # output directory for plots
MEMORY_SOURCE = "logger"                       # "logger" or "total"

def parse_watch_file(filepath: str, memory_source: str) -> Tuple[pd.DataFrame, pd.DataFrame]:
    events: List[dict] = []
    resources: List[dict] = []
    fname = os.path.basename(filepath)

    with open(filepath) as f:
        first_gen_ts = None
        first_wch_ts = None
        first_res_ts = None
        for line in f:
            rec = json.loads(line)
            if "packet_id" in rec:
                # Event record
                if first_gen_ts is None:
                    first_gen_ts = rec["generator_ts"]
                if first_wch_ts is None:
                    first_wch_ts = rec["watcher_ts"]
                gen_s = (rec["generator_ts"] - first_gen_ts) / 1e6
                done_s = (rec["watcher_ts"] - first_wch_ts) / 1e6
                lat = (rec["watcher_ts"] - rec["generator_ts"]) / 1e3
                events.append({
                    "file": fname,
                    "gen_second": int(gen_s),
                    "done_second": int(done_s),
                    "latency_ms": lat,
                })
            else:
                # Resource record
                if first_res_ts is None:
                    first_res_ts = rec["timestamp"]
                t = (rec["timestamp"] - first_res_ts) / 1e6
                cpu = rec.get("total_cpu")
                total_mem = rec.get("total_memory")
                logger_mem = rec.get("logger_memory")
                # Choose memory source: convert from KiB to MB
                if memory_source == "logger" and logger_mem is not None:
                    mem_val = float(logger_mem) / 1024.0
                elif total_mem is not None:
                    mem_val = float(total_mem) / 1024.0
                else:
                    mem_val = np.nan
                resources.append({
                    "file": fname,
                    "second": int(t),
                    "cpu": float(cpu) if cpu is not None else np.nan,
                    "mem": mem_val,
                })
    events_df = pd.DataFrame(events)
    resources_df = pd.DataFrame(resources)
    return events_df, resources_df


def load_watch_files(basedir: Path, memory_source: str) -> Tuple[pd.DataFrame, pd.DataFrame]:
    """Load and parse all .json.watch files in a directory.

    Returns combined DataFrames for events and resources.  If no files are
    found, raises SystemExit.
    """
    events_list: List[pd.DataFrame] = []
    res_list: List[pd.DataFrame] = []
    if not basedir.exists():
        raise SystemExit(f"Eingabeverzeichnis nicht gefunden: {basedir}")
    for fname in sorted(os.listdir(basedir)):
        if fname.endswith(".json.watch"):
            e, r = parse_watch_file(os.path.join(basedir, fname), memory_source)
            if not e.empty:
                events_list.append(e)
            if not r.empty:
                res_list.append(r)
    if not events_list:
        raise SystemExit("Keine passenden Event-Zeilen in den .json.watch-Dateien gefunden.")
    events_df = pd.concat(events_list, ignore_index=True)
    res_df = pd.concat(res_list, ignore_index=True) if res_list else pd.DataFrame(columns=["file","second","cpu","mem"])
    return events_df, res_df


def print_latency_summary(events_df: pd.DataFrame) -> None:
    """Print summary statistics of latencies across all runs."""
    if events_df.empty:
        print("Keine Event-Daten vorhanden; keine Latenzstatistiken.")
        return
    lat = events_df["latency_ms"].dropna()
    stats = {
        "mean_ms": np.mean(lat),
        "median_ms": np.median(lat),
        "std_ms": np.std(lat),
        "p99_ms": np.percentile(lat, 99),
        "max_ms": np.max(lat),
        "count": len(lat),
    }
    print("Latency summary (all runs, ms):")
    for k, v in stats.items():
        if k == "count":
            print(f"  {k}: {v}")
        else:
            print(f"  {k}: {v:.3f}")


def print_throughput(events_df: pd.DataFrame) -> None:
    """Print processed and generated throughput per second across runs."""
    if events_df.empty:
        print("Keine Event-Daten vorhanden; kein Durchsatz verfügbar.")
        return
    print("\nProcessed vs. Generated throughput (Events/s):")
    # Compute processed throughput per run
    done_counts = events_df.groupby(["file", "done_second"]).size().rename("count").reset_index()
    gen_counts  = events_df.groupby(["file", "gen_second"]).size().rename("count").reset_index()
    # Aggregate across runs
    done_stats = (done_counts.groupby("done_second")
                  .agg(mean_rate=("count", "mean"),
                       min_rate=("count", "min"),
                       max_rate=("count", "max"))
                  .reset_index()
                  .rename(columns={"done_second": "second"}))
    gen_stats  = (gen_counts.groupby("gen_second")
                  .agg(mean_rate=("count", "mean"),
                       min_rate=("count", "min"),
                       max_rate=("count", "max"))
                  .reset_index()
                  .rename(columns={"gen_second": "second"}))
    # Print table header
    print(f"{'Sec':>4} | {'Proc mean':>9} {'min':>5} {'max':>5} | {'Gen mean':>9} {'min':>5} {'max':>5}")
    print("-" * 58)
    # Merge on second to iterate through full range
    max_sec = int(max(done_stats["second"].max() if not done_stats.empty else 0,
                      gen_stats["second"].max() if not gen_stats.empty else 0))
    for sec in range(max_sec + 1):
        done_row = done_stats[done_stats["second"] == sec]
        gen_row  = gen_stats[gen_stats["second"] == sec]
        d_mean = done_row["mean_rate"].iloc[0] if not done_row.empty else 0
        d_min  = done_row["min_rate"].iloc[0] if not done_row.empty else 0
        d_max  = done_row["max_rate"].iloc[0] if not done_row.empty else 0
        g_mean = gen_row["mean_rate"].iloc[0] if not gen_row.empty else 0
        g_min  = gen_row["min_rate"].iloc[0] if not gen_row.empty else 0
        g_max  = gen_row["max_rate"].iloc[0] if not gen_row.empty else 0
        print(f"{sec:>4} | {d_mean:9.1f} {d_min:5.0f} {d_max:5.0f} | {g_mean:9.1f} {g_min:5.0f} {g_max:5.0f}")


def aggregate_per_second(df: pd.DataFrame, value_col: str) -> pd.DataFrame:
    """Aggregate per-second values across runs."""
    if df.empty:
        return pd.DataFrame(columns=["second", "mean", "min", "max"])
    # Ensure numeric type
    numeric = df[value_col].astype(float)
    tmp = df.copy()
    tmp[value_col] = numeric
    # Compute per-run mean per second
    per_run = tmp.groupby(["file", "second"])[value_col].mean().reset_index()
    # Aggregate across runs
    agg = (per_run.groupby("second")[value_col]
           .agg(mean="mean", min="min", max="max")
           .reset_index())
    return agg


def aggregate_median_latency(events_df: pd.DataFrame) -> pd.DataFrame:
    """Compute aggregated median latency per second across runs."""
    if events_df.empty:
        return pd.DataFrame(columns=["second", "mean", "min", "max"])
    lat_per_run = (events_df.groupby(["file", "gen_second"])
                   ["latency_ms"].median().reset_index()
                   .rename(columns={"gen_second": "second"}))
    agg = (lat_per_run.groupby("second")["latency_ms"]
           .agg(mean="mean", min="min", max="max")
           .reset_index())
    return agg


def aggregate_event_rate(events_df: pd.DataFrame) -> pd.DataFrame:
    """Compute aggregated generated event rate (events per second) across runs."""
    if events_df.empty:
        return pd.DataFrame(columns=["second", "mean_rate", "min_rate", "max_rate"])
    gen_counts = events_df.groupby(["file", "gen_second"]).size().reset_index(name="count")
    gen_counts = gen_counts.rename(columns={"gen_second": "second"})
    agg = (gen_counts.groupby("second")["count"]
           .agg(mean_rate="mean", min_rate="min", max_rate="max")
           .reset_index())
    return agg


# ===== Plotting (aligned style) =====

def plot_cpu_memory(cpu_agg: pd.DataFrame, mem_agg: pd.DataFrame, outdir: Path) -> None:
    """Plot aggregated CPU and memory usage over time."""
    apply_plot_style(fig_width=8, fig_height=4, enable_grid=True, tight=True)
    if not cpu_agg.empty:
        plt.figure()
        plt.plot(cpu_agg["second"], cpu_agg["mean"], linewidth=1.5, label="CPU mean")
        plt.fill_between(cpu_agg["second"], cpu_agg["min"], cpu_agg["max"], alpha=0.2, label="CPU min–max")
        plt.xlabel("Time (s)")
        plt.ylabel("CPU usage (%)")
        plt.title("CPU usage over time (C++ Port)")
        plt.legend(loc="upper right")
        finalize_plot(outdir / "cpu_usage_cpp.png")
    if not mem_agg.empty:
        plt.figure()
        plt.plot(mem_agg["second"], mem_agg["mean"], linewidth=1.5, label="Memory mean")
        plt.fill_between(mem_agg["second"], mem_agg["min"], mem_agg["max"], alpha=0.2, label="Memory min–max")
        plt.xlabel("Time (s)")
        plt.ylabel("Memory usage (MB)")
        plt.title("Memory usage over time (C++ Port)")
        plt.legend(loc="upper right")
        finalize_plot(outdir / "memory_usage_cpp.png")


def plot_latency_eventrate(lat_agg: pd.DataFrame, evt_rate: pd.DataFrame, outdir: Path) -> None:
    """Plot latency (median per second) and event rate over time."""
    if lat_agg.empty and evt_rate.empty:
        return
    apply_plot_style(fig_width=8, fig_height=4, enable_grid=True, tight=True)
    fig, ax1 = plt.subplots()
    if not lat_agg.empty:
        ax1.plot(lat_agg["second"], lat_agg["mean"], linewidth=1.5, label="Latency")
        ax1.fill_between(lat_agg["second"], lat_agg["min"], lat_agg["max"], alpha=0.2, label="Latency (min–max)")
        ax1.set_ylabel("Median latency (ms)")
    ax1.set_xlabel("Time (s)")
    ax1.set_title("Latency and event rate over time (C++ Port)")
    # Event rate on second axis
    if not evt_rate.empty:
        ax2 = ax1.twinx()
        ax2.plot(evt_rate["second"], evt_rate["mean_rate"], linewidth=1.0, label="Event rate")
        ax2.set_ylabel("Events per second")
    # Legend (handles from primary axis)
    lines1, labels1 = ax1.get_legend_handles_labels()
    if lines1:
        ax1.legend(lines1, labels1, loc="upper right")
    finalize_plot(outdir / "latency_eventrate_cpp.png")


def plot_throughput_latency(events_df: pd.DataFrame, lat_agg: pd.DataFrame, outdir: Path) -> None:
    """Plot processed vs. generated throughput along with latency."""
    if events_df.empty:
        return
    # Compute throughput per second per run
    done_counts = events_df.groupby(["file", "done_second"]).size().reset_index(name="count").rename(columns={"done_second": "second"})
    gen_counts  = events_df.groupby(["file", "gen_second"]).size().reset_index(name="count").rename(columns={"gen_second": "second"})
    # Aggregate across runs
    done_agg = (done_counts.groupby("second").agg(mean_rate=("count", "mean"),
                                                  min_rate=("count", "min"),
                                                  max_rate=("count", "max")).reset_index())
    gen_agg  = (gen_counts.groupby("second").agg(mean_rate=("count", "mean"),
                                                 min_rate=("count", "min"),
                                                 max_rate=("count", "max")).reset_index())
    apply_plot_style(fig_width=8, fig_height=4, enable_grid=True, tight=True)
    fig, ax1 = plt.subplots()
    # processed throughput
    if not done_agg.empty:
        ax1.plot(done_agg["second"], done_agg["mean_rate"], linewidth=1.8, label="Processed")
        ax1.fill_between(done_agg["second"], done_agg["min_rate"], done_agg["max_rate"], alpha=0.15, label="Processed (min–max)")
    # generated throughput
    if not gen_agg.empty:
        ax1.plot(gen_agg["second"], gen_agg["mean_rate"], linestyle="--", linewidth=1.5, label="Generated")
        ax1.fill_between(gen_agg["second"], gen_agg["min_rate"], gen_agg["max_rate"], alpha=0.10, label="Generated (min–max)")
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Events per second")
    # Right axis: latency
    if not lat_agg.empty:
        ax2 = ax1.twinx()
        ax2.plot(lat_agg["second"], lat_agg["mean"], linewidth=1.5, label="Latency")
        ax2.fill_between(lat_agg["second"], lat_agg["min"], lat_agg["max"], alpha=0.10, label="Latency (min–max)")
        ax2.set_ylabel("Median latency (ms)")
    # Legend
    lines1, labels1 = ax1.get_legend_handles_labels()
    if lines1:
        ax1.legend(lines1, labels1, loc="upper right")
    plt.title("Throughput and latency over time (C++ Port)")
    finalize_plot(outdir / "throughput_processed_generated_latency_cpp.png")


def plot_latency_distribution(events_df: pd.DataFrame, outdir: Path) -> None:
    """Plot a boxplot of latency distribution across all runs."""
    if events_df.empty:
        return
    lat_series = events_df["latency_ms"].dropna()
    if lat_series.empty:
        return
    apply_plot_style(fig_width=8, fig_height=2.5, enable_grid=False, tight=True)
    plt.figure()
    # keep defaults to match overall style; minimal customization
    plt.boxplot(lat_series, vert=False, whis=[5,95], showfliers=False, patch_artist=False)
    median = float(np.median(lat_series))
    plt.scatter([median], [1], s=40, zorder=3, label=f"Median: {median:.2f} ms")
    plt.xlabel("Latency (ms)")
    plt.yticks([])
    plt.title("Latency distribution (C++ Port)")
    plt.legend(loc="upper right")
    finalize_plot(outdir / "latency_boxplot_cpp.png")


def main():
    events_df, res_df = load_watch_files(BASEDIR, MEMORY_SOURCE)
    OUTDIR.mkdir(parents=True, exist_ok=True)
    # Print summary statistics
    print_latency_summary(events_df)
    print_throughput(events_df)
    # Aggregations
    cpu_agg = aggregate_per_second(res_df, "cpu")
    mem_agg = aggregate_per_second(res_df, "mem")
    lat_agg = aggregate_median_latency(events_df)
    evt_rate = aggregate_event_rate(events_df)
    # Plots
    plot_cpu_memory(cpu_agg, mem_agg, OUTDIR)
    plot_latency_eventrate(lat_agg, evt_rate, OUTDIR)
    plot_throughput_latency(events_df, lat_agg, OUTDIR)
    plot_latency_distribution(events_df, OUTDIR)


if __name__ == "__main__":
    main()
