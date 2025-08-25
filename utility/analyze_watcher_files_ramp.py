#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os, json
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ================= Einstellungen =================
basedir = "../benchmark_results"   # Ordner mit .json.watch-Dateien
memory_source = "logger"                # "logger" oder "total"
outdir = "plots"
os.makedirs(outdir, exist_ok=True)

# ================= Parser =================
def parse_watch(path):
    """Einlesen einer .json.watch-Datei, getrennt nach Events und Ressourcen."""
    events, resources = [], []
    name = os.path.basename(path)
    lang = "Python" if "python" in name.lower() else "C++ Port"
    with open(path) as f:
        first_gen_ts = None
        first_res_ts = None
        for line in f:
            rec = json.loads(line)
            if "packet_id" in rec:
                if first_gen_ts is None:
                    first_gen_ts = rec["generator_ts"]
                t = (rec["generator_ts"] - first_gen_ts) / 1e6
                lat = (rec["watcher_ts"] - rec["generator_ts"]) / 1e3
                events.append({
                    "language": lang, "file": name,
                    "second": int(t), "latency_ms": lat
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
                    mem_val = float(logger_mem)/1024
                elif total_mem is not None:
                    mem_val = float(total_mem)/1024
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
        events_list.append(e)
        res_list.append(r)
events_df = pd.concat(events_list, ignore_index=True)
res_df    = pd.concat(res_list,   ignore_index=True)

# ================= Aggregation =================
# Ereignisrate pro Sekunde
evt_counts = events_df.groupby(["language","file","second"]).size().reset_index(name="count")
evt_agg    = evt_counts.groupby(["language","second"]) \
    .agg(mean_rate=("count","mean")) \
    .reset_index()

# CPU je Sekunde mitteln und über Läufe aggregieren
cpu_per = res_df.groupby(["language","file","second"]).cpu.mean().reset_index()
cpu_agg = cpu_per.groupby(["language","second"]).agg(
    mean_cpu=("cpu","mean"),
    min_cpu=("cpu","min"),
    max_cpu=("cpu","max")
).reset_index()

# Memory je Sekunde mitteln und über Läufe aggregieren
mem_per = res_df.groupby(["language","file","second"]).mem.mean().reset_index()
mem_agg = mem_per.groupby(["language","second"]).agg(
    mean_mem=("mem","mean"),
    min_mem=("mem","min"),
    max_mem=("mem","max")
).reset_index()

# Median-Latenz pro Sekunde
lat_per = events_df.groupby(["language","file","second"]).latency_ms.median().reset_index()
lat_agg = lat_per.groupby(["language","second"]).agg(
    mean_lat=("latency_ms","mean"),
    min_lat=("latency_ms","min"),
    max_lat=("latency_ms","max")
).reset_index()

# ================= Plots =================
colors = {"Python": "tab:blue", "C++ Port": "tab:orange"}

# CPU Plot kombiniert
plt.figure(figsize=(8,4))
for lang in ["Python","C++ Port"]:
    sub = cpu_agg[cpu_agg.language==lang]
    c = colors[lang]
    plt.plot(sub["second"], sub["mean_cpu"], color=c, lw=1.5)
    plt.plot(sub["second"], sub["min_cpu"], color=c, ls="--", lw=0.8)
    plt.plot(sub["second"], sub["max_cpu"], color=c, ls="--", lw=0.8)
plt.xlabel("Time (s)")
plt.ylabel("CPU usage (%)")
plt.title("CPU usage over time")
plt.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)   # Gitter für beide Achsen
plt.tight_layout()
plt.savefig(os.path.join(outdir,"cpu_usage_combined_ramp.png"), dpi=150)
plt.close()

# Memory Plot kombiniert
plt.figure(figsize=(8,4))
for lang in ["Python","C++ Port"]:
    sub = mem_agg[mem_agg.language==lang]
    c = colors[lang]
    plt.plot(sub["second"], sub["mean_mem"], color=c, lw=1.5)
    plt.plot(sub["second"], sub["min_mem"], color=c, ls="--", lw=0.8)
    plt.plot(sub["second"], sub["max_mem"], color=c, ls="--", lw=0.8)
plt.xlabel("Time (s)")
plt.ylabel("Memory usage (MB)")
plt.title("Memory usage over time")
plt.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)   # Gitter
plt.tight_layout()
plt.savefig(os.path.join(outdir,"memory_usage_combined_ramp.png"), dpi=150)
plt.close()

# Latency + Eventrate kombiniert
fig, ax1 = plt.subplots(figsize=(8,4))
for lang in ["Python","C++ Port"]:
    sub = lat_agg[lat_agg.language==lang]
    c = colors[lang]
    ax1.plot(sub["second"], sub["mean_lat"], color=c, lw=1.5)
    ax1.plot(sub["second"], sub["min_lat"], color=c, ls="--", lw=0.8)
    ax1.plot(sub["second"], sub["max_lat"], color=c, ls="--", lw=0.8)
ax1.set_xlabel("Time (s)")
ax1.set_ylabel("Median latency (ms)")
ax1.set_title("Latency and event rate over time")
ax1.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)   # Gitter

ax2 = ax1.twinx()
rate = evt_agg[evt_agg.language=="Python"]  # identisch für beide
ax2.plot(rate["second"], rate["mean_rate"], color="grey", lw=1.2)
ax2.set_ylabel("Event rate (events/s)")

plt.tight_layout()
plt.savefig(os.path.join(outdir,"latency_eventrate_combined_ramp.png"), dpi=150)
plt.close()
