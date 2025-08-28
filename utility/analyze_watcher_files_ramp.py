import os, json
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ================= Einstellungen =================
basedir = "../benchmark_results/ramp_cpp_1000to10000"   # Ordner mit .json.watch-Dateien
memory_source = "logger"           # "logger" oder "total"
outdir = "plots"
os.makedirs(outdir, exist_ok=True)

# ================= Parser =================
def parse_watch(path):
    """Einlesen einer .json.watch-Datei, getrennt nach Events und Ressourcen."""
    events, resources = [], []
    name = os.path.basename(path)
    lang = "Python" if 'json' in name.lower() else "C++ Port"
    with open(path) as f:
        first_gen_ts = None
        first_wch_ts = None
        first_res_ts = None
        for line in f:
            rec = json.loads(line)
            if "packet_id" in rec:
                if first_gen_ts is None:
                    first_gen_ts = rec["generator_ts"]
                if first_wch_ts is None:
                    first_wch_ts = rec["watcher_ts"]

                gen_s  = (rec["generator_ts"] - first_gen_ts) / 1e6
                done_s = (rec["watcher_ts"]  - first_wch_ts)  / 1e6
                lat    = (rec["watcher_ts"]  - rec["generator_ts"]) / 1e3

                events.append({
                    "language":   lang,
                    "file":       name,
                    "gen_second":  int(gen_s),
                    "done_second": int(done_s),
                    "latency_ms":  lat
                })

            else:
                if first_res_ts is None:
                    first_res_ts = rec["timestamp"]
                t = (rec["timestamp"] - first_res_ts) / 1e6
                cpu = rec.get("total_cpu")
                total_mem  = rec.get("total_memory")
                logger_mem = rec.get("logger_memory")
                mem_val = None
                if memory_source == "logger" and logger_mem is not None:
                    mem_val = float(logger_mem)/1024.0
                elif total_mem is not None:
                    mem_val = float(total_mem)/1024.0
                resources.append({
                    "language": lang, "file": name,
                    "second": int(t),
                    "cpu": float(cpu) if cpu is not None else np.nan,
                    "mem": mem_val
                })
    return pd.DataFrame(events), pd.DataFrame(resources)

# ================= Daten einlesen =================
events_list, res_list = [], []
for fname in sorted(os.listdir(basedir)):
    if fname.endswith(".json.watch"):
        e, r = parse_watch(os.path.join(basedir, fname))
        if not e.empty: events_list.append(e)
        if not r.empty: res_list.append(r)

# NUR Python-Events für Durchsatz relevant
if events_list:
    events_df = pd.concat(events_list, ignore_index=True)
else:
    raise SystemExit("Keine passenden Event-Zeilen in den .json.watch-Dateien gefunden.")
if res_list:
    res_df    = pd.concat(res_list,   ignore_index=True)
else:
    res_df = pd.DataFrame(columns=["language","file","second","cpu","mem"])

# ================= Durchsatz (nur Python ausgeben) =================
def print_throughput(events_df):
    print("\n==================== THROUGHPUT (nur Python) ====================")
    if "language" not in events_df.columns:
        print("Keine Sprache definiert; Durchsatzausgabe übersprungen.")
        return
    if "Python" not in events_df["language"].unique():
        print("Keine Python-Daten gefunden.")
        return

    lang = "Python"
    sub = events_df[events_df["language"] == lang]

    def complete_range(s):
        if s.empty:
            return s
        idx = pd.RangeIndex(s.index.min(), s.index.max() + 1, 1)
        return s.reindex(idx, fill_value=0)

    # processed throughput (watcher_ts)
    done = (sub.groupby(["file", "done_second"]).size()
            .rename("processed").to_frame())
    per_run_done = []
    for run, d in done.groupby(level="file"):
        s = d["processed"]
        # ==> WICHTIG: den 'file'-Level vom Index entfernen
        if isinstance(s.index, pd.MultiIndex):
            s = s.droplevel("file")
        s.index = s.index.rename("second")
        s = complete_range(s)
        s.name = run
        per_run_done.append(s)
    if per_run_done:
        done_mat = pd.concat(per_run_done, axis=1).fillna(0).astype(int)
        done_stats = pd.DataFrame({
            "mean": done_mat.mean(axis=1),
            "min":  done_mat.min(axis=1),
            "max":  done_mat.max(axis=1),
        })
    else:
        done_stats = pd.DataFrame()

    # generated rate (generator_ts)
    gen = (sub.groupby(["file", "gen_second"]).size()
           .rename("generated").to_frame())
    per_run_gen = []
    for run, g in gen.groupby(level="file"):
        s = g["generated"]
        # ==> ebenfalls den 'file'-Level entfernen
        if isinstance(s.index, pd.MultiIndex):
            s = s.droplevel("file")
        s.index = s.index.rename("second")
        s = complete_range(s)
        s.name = run
        per_run_gen.append(s)
    if per_run_gen:
        gen_mat = pd.concat(per_run_gen, axis=1).fillna(0).astype(int)
        gen_stats = pd.DataFrame({
            "mean": gen_mat.mean(axis=1),
            "min":  gen_mat.min(axis=1),
            "max":  gen_mat.max(axis=1),
        })
    else:
        gen_stats = pd.DataFrame()

    # Ausgabe
    if not done_stats.empty:
        print("VERARBEITET (watcher_ts / done_second) – Events/s:")
        for sec, row in done_stats.iterrows():
            print(f"Sekunde {int(sec):>5}: {row['mean']:.1f} ev/s  (min {row['min']:.0f}, max {row['max']:.0f})")
    else:
        print("Keine verarbeiteten Events für Python gefunden.")

    if not gen_stats.empty:
        print("\nGENERATED (generator_ts / gen_second) – nur Referenz:")
        for sec, row in gen_stats.iterrows():
            print(f"Sekunde {int(sec):>5}: {row['mean']:.1f} ev/s  (min {row['min']:.0f}, max {row['max']:.0f})")
    else:
        print("\nKeine generierten Events für Python gefunden.")

print_throughput(events_df)

# ================= Aggregation für Plots =================
# Ereignisrate (nur generator_ts, wie bisher)
evt_counts = events_df.groupby(["language","file","gen_second"]).size().reset_index(name="count")
evt_counts = evt_counts.rename(columns={"gen_second": "second"})
evt_agg    = evt_counts.groupby(["language","second"]) \
    .agg(mean_rate=("count","mean")) \
    .reset_index()

# CPU je Sekunde mitteln und über Läufe aggregieren
if not res_df.empty:
    cpu_per = res_df.groupby(["language","file","second"]).cpu.mean().reset_index()
    cpu_agg = cpu_per.groupby(["language","second"]).agg(
        mean_cpu=("cpu","mean"),
        min_cpu=("cpu","min"),
        max_cpu=("cpu","max")
    ).reset_index()
else:
    cpu_agg = pd.DataFrame(columns=["language","second","mean_cpu","min_cpu","max_cpu"])

# Memory je Sekunde mitteln und über Läufe aggregieren
if not res_df.empty:
    mem_per = res_df.groupby(["language","file","second"]).mem.mean().reset_index()
    mem_agg = mem_per.groupby(["language","second"]).agg(
        mean_mem=("mem","mean"),
        min_mem=("mem","min"),
        max_mem=("mem","max")
    ).reset_index()
else:
    mem_agg = pd.DataFrame(columns=["language","second","mean_mem","min_mem","max_mem"])

# Median-Latenz pro Sekunde (nach generator_ts)
lat_per = events_df.groupby(["language","file","gen_second"]).latency_ms.median().reset_index()
lat_per = lat_per.rename(columns={"gen_second": "second"})
lat_agg = lat_per.groupby(["language","second"]).agg(
    mean_lat=("latency_ms","mean"),
    min_lat=("latency_ms","min"),
    max_lat=("latency_ms","max")
).reset_index()

# ================= Plots (unverändert) =================
colors = {"Python": "tab:blue", "C++ Port": "tab:orange"}

# CPU Plot kombiniert
plt.figure(figsize=(8,4))
for lang in ["Python","C++ Port"]:
    sub = cpu_agg[cpu_agg.language==lang]
    if sub.empty: continue
    c = colors[lang]
    plt.plot(sub["second"], sub["mean_cpu"], color=c, lw=1.5)
    plt.plot(sub["second"], sub["min_cpu"], color=c, ls="--", lw=0.8)
    plt.plot(sub["second"], sub["max_cpu"], color=c, ls="--", lw=0.8)
plt.xlabel("Time (s)")
plt.ylabel("CPU usage (%)")
plt.title("CPU usage over time")
plt.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)
plt.tight_layout()
plt.savefig(os.path.join(outdir,"cpu_usage_combined_ramp.png"), dpi=150)
plt.close()

# Memory Plot kombiniert
plt.figure(figsize=(8,4))
for lang in ["Python","C++ Port"]:
    sub = mem_agg[mem_agg.language==lang]
    if sub.empty: continue
    c = colors[lang]
    plt.plot(sub["second"], sub["mean_mem"], color=c, lw=1.5)
    plt.plot(sub["second"], sub["min_mem"], color=c, ls="--", lw=0.8)
    plt.plot(sub["second"], sub["max_mem"], color=c, ls="--", lw=0.8)
plt.xlabel("Time (s)")
plt.ylabel("Memory usage (MB)")
plt.title("Memory usage over time")
plt.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)
plt.tight_layout()
plt.savefig(os.path.join(outdir,"memory_usage_combined_ramp.png"), dpi=150)
plt.close()

# Latency + Eventrate kombiniert
fig, ax1 = plt.subplots(figsize=(8,4))
for lang in ["Python","C++ Port"]:
    sub = lat_agg[lat_agg.language==lang]
    if sub.empty: continue
    c = colors[lang]
    ax1.plot(sub["second"], sub["mean_lat"], color=c, lw=1.5)
    ax1.plot(sub["second"], sub["min_lat"], color=c, ls="--", lw=0.8)
    ax1.plot(sub["second"], sub["max_lat"], color=c, ls="--", lw=0.8)
ax1.set_xlabel("Time (s)")
ax1.set_ylabel("Median latency (ms)")
ax1.set_title("Latency and event rate over time")
ax1.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)

ax2 = ax1.twinx()
rate = evt_agg[evt_agg.language=="Python"]  # identisch für beide
if not rate.empty:
    ax2.plot(rate["second"], rate["mean_rate"], color="grey", lw=1.2)
ax2.set_ylabel("Event rate (events/s)")

plt.tight_layout()
plt.savefig(os.path.join(outdir,"latency_eventrate_combined_ramp.png"), dpi=150)
plt.close()

# ==== Throughput: processed (heiDPI) vs. generated (Referenz) + Latenz (rechte Achse) ====
lang = "Python"
sub = events_df[events_df["language"] == lang].copy()
if sub.empty:
    print("Keine Python-Daten für Throughput-Plot gefunden.")
else:
    # Counts je Sekunde und Lauf
    done_counts = (sub.groupby(["file","done_second"])
                   .size().reset_index(name="count")
                   .rename(columns={"done_second":"second"}))
    gen_counts  = (sub.groupby(["file","gen_second"])
                   .size().reset_index(name="count")
                   .rename(columns={"gen_second":"second"}))

    # Über Läufe aggregieren (mean/min/max) zu Sekunde
    done_agg = (done_counts.groupby("second")
                .agg(mean_rate=("count","mean"),
                     min_rate=("count","min"),
                     max_rate=("count","max"))
                .reset_index())
    gen_agg  = (gen_counts.groupby("second")
                .agg(mean_rate=("count","mean"),
                     min_rate=("count","min"),
                     max_rate=("count","max"))
                .reset_index())

    # Latenz (bereits aggregiert in lat_agg): nur Python
    lat_sub = lat_agg[lat_agg.language == lang].copy()

    # Farben definieren
    col_processed = "tab:blue"
    col_generator = "tab:orange"
    col_latency   = "tab:green"

    # Plot
    fig, ax1 = plt.subplots(figsize=(8,4))
    lines = []
    labels = []

    # heiDPI processed
    if not done_agg.empty:
        l1, = ax1.plot(done_agg["second"], done_agg["mean_rate"], lw=1.8,
                       color=col_processed, label="heiDPI processed")
        ax1.fill_between(done_agg["second"], done_agg["min_rate"], done_agg["max_rate"],
                         color=col_processed, alpha=0.15, label="heiDPI processed (min–max)")
        lines.append(l1); labels.append(l1.get_label())

    # Generator (Referenz)
    if not gen_agg.empty:
        l2, = ax1.plot(gen_agg["second"], gen_agg["mean_rate"], ls="--", lw=1.5,
                       color=col_generator, label="Generator")
        ax1.fill_between(gen_agg["second"], gen_agg["min_rate"], gen_agg["max_rate"],
                         color=col_generator, alpha=0.10, label="Generator (min–max)")
        lines.append(l2); labels.append(l2.get_label())

    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Events per second", color="black")
    ax1.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)

    # Rechte Achse: Latenz
    ax2 = ax1.twinx()
    if not lat_sub.empty:
        l3, = ax2.plot(lat_sub["second"], lat_sub["mean_lat"], lw=1.5,
                       color=col_latency, label="Latency")
        ax2.fill_between(lat_sub["second"], lat_sub["min_lat"], lat_sub["max_lat"],
                         color=col_latency, alpha=0.10, label="Latency (min–max)")
        ax2.set_ylabel("Median latency (ms)", color=col_latency)
        lines.append(l3); labels.append(l3.get_label())

    # Gemeinsame Legende
    ax1.legend(lines, labels, loc="upper right")

    plt.title("Throughput: heiDPI processed vs. Generator + Latency")
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "throughput_processed_generated_latency_ramp.png"), dpi=150)
    plt.close()
