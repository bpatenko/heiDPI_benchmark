import os, json
import numpy as np
import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt

# ================= Einstellungen =================
basedir = "../benchmark_results"   # Ordner mit .json.watch (nur Python-Lauf)
print_full_throughput = True   # True = jede Sekunde ausgeben; False = nur Zusammenfassung

# ================= Parser =================
def parse_watch(path):
    """
    Liest eine .json.watch-Datei und liefert Event-Zeiten relativ zum Start:
      - gen_second  : Sekunde der Erzeugung (generator_ts)
      - done_second : Sekunde der Fertigstellung (watcher_ts)
    """
    rows = []
    first_gen_ts = None
    first_wch_ts = None
    with open(path) as f:
        for line in f:
            rec = json.loads(line)
            if "packet_id" not in rec:
                continue
            if first_gen_ts is None:
                first_gen_ts = rec["generator_ts"]
            if first_wch_ts is None:
                first_wch_ts = rec["watcher_ts"]
            gen_s  = (rec["generator_ts"] - first_gen_ts) / 1e6
            done_s = (rec["watcher_ts"]  - first_wch_ts)  / 1e6
            rows.append({
                "gen_second":  int(gen_s),
                "done_second": int(done_s),
            })
    return pd.DataFrame(rows)

# ================= Daten einlesen =================
watch_dir = Path(basedir)
files = sorted([p for p in watch_dir.iterdir()
                if p.suffix == ".watch" or p.name.endswith(".json.watch")])
if not files:
    raise SystemExit(f"Keine .watch-Dateien in {basedir} gefunden.")

dfs = []
for p in files:
    df = parse_watch(p)
    if not df.empty:
        dfs.append(df)

if not dfs:
    raise SystemExit("Keine Events in den .watch-Dateien gefunden.")

events = pd.concat(dfs, ignore_index=True)

# ================= Throughput-Berechnungen =================
# 1) Verarbeitet pro Sekunde (nach watcher_ts / done_second)
thru_done = (events.groupby("done_second").size()
             .rename("processed_events").to_frame())

# 2) Erzeugt pro Sekunde (nach generator_ts / gen_second) – nur als Referenz
rate_gen = (events.groupby("gen_second").size()
            .rename("generated_events").to_frame())

# Sekundenskala vereinheitlichen und Lücken mit 0 füllen
start_s = 0
end_s   = int(max(thru_done.index.max() if len(thru_done) else 0,
                  rate_gen.index.max()  if len(rate_gen)  else 0))
index = pd.RangeIndex(start_s, end_s + 1, 1)
thru_done = thru_done.reindex(index, fill_value=0)
rate_gen  = rate_gen.reindex(index,  fill_value=0)

# Optional: Backlog (kumulativ erzeugt – kumulativ verarbeitet)
cum_gen  = rate_gen["generated_events"].cumsum()
cum_done = thru_done["processed_events"].cumsum()
backlog  = (cum_gen - cum_done).rename("backlog_events")

# ================= Ausgabe =================
print("\n==== Throughput (VERARBEITET, watcher_ts) – Events pro Sekunde ====")
if print_full_throughput:
    for sec, val in thru_done["processed_events"].items():
        print(f"Sekunde {sec:>5}: {int(val)} Events/s verarbeitet")
else:
    print("Erste 10 s:")
    for sec, val in thru_done["processed_events"].iloc[:10].items():
        print(f"  {sec:>5}: {int(val)}")
    print("…")
    print("Letzte 10 s:")
    for sec, val in thru_done["processed_events"].iloc[-10:].items():
        print(f"  {sec:>5}: {int(val)}")

print("\n==== Erzeugungsrate (GENERIERT, generator_ts) – nur Referenz ====")
if print_full_throughput:
    for sec, val in rate_gen["generated_events"].items():
        print(f"Sekunde {sec:>5}: {int(val)} Events/s generiert")
else:
    print("Erste 10 s:")
    for sec, val in rate_gen["generated_events"].iloc[:10].items():
        print(f"  {sec:>5}: {int(val)}")
    print("…")
    print("Letzte 10 s:")
    for sec, val in rate_gen["generated_events"].iloc[-10:].items():
        print(f"  {sec:>5}: {int(val)}")

        # === Plot: Throughput processed vs. generated (Overload) ===
outdir = "plots"
import matplotlib.pyplot as plt

seconds   = thru_done.index.values
processed = thru_done["processed_events"].values
generated = rate_gen["generated_events"].values

# Zwei Achsen mit geteilter X-Achse
fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(8,4),
                               gridspec_kw={'height_ratios':[1,5]})

# oberer Bereich (z.B. 200–520)
ax1.plot(seconds, processed, lw=1.8, label="heiDPI")
ax1.plot(seconds, generated, ls="--", lw=1.5, label="Generator")
ax1.set_ylim(490, 510)   # oberer Bereich

# unterer Bereich (0–170)
ax2.plot(seconds, processed, lw=1.8)
ax2.plot(seconds, generated, ls="--", lw=1.5)
ax2.set_ylim(0, 170)     # unterer Bereich

# kleine diagonale "Break"-Markierungen
d = .015
kwargs = dict(transform=ax1.transAxes, color='k', clip_on=False)
ax1.plot((-d,+d), (-d,+d), **kwargs)        # links unten
ax1.plot((1-d,1+d), (-d,+d), **kwargs)      # rechts unten

kwargs.update(transform=ax2.transAxes)
ax2.plot((-d,+d), (1-d,1+d), **kwargs)      # links oben
ax2.plot((1-d,1+d), (1-d,1+d), **kwargs)    # rechts oben

# Labels, Titel
ax2.set_xlabel("Time (s)")
ax1.set_ylabel("Events/s")
ax2.set_ylabel("Events/s")
ax1.legend()
fig.suptitle("Throughput (Overload)")
fig.tight_layout()
fig.savefig(os.path.join(outdir,"throughput_overload_broken_yaxis.png"), dpi=150)
plt.close()


# === Plot: Throughput (0–50 s, gebrochene Y-Achse) ===
mask = seconds <= 50
sec50 = seconds[mask]
proc50 = processed[mask]
gen50  = generated[mask]

fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(8,4),
                               gridspec_kw={'height_ratios':[1,5]})

# oberer Bereich (z.B. 490–510)
ax1.plot(sec50, proc50, lw=1.8, label="heiDPI")
ax1.plot(sec50, gen50,  ls="--", lw=1.5, label="Generator")
ax1.set_ylim(490, 510)

# unterer Bereich (0–170)
ax2.plot(sec50, proc50, lw=1.8)
ax2.plot(sec50, gen50,  ls="--", lw=1.5)
ax2.set_ylim(0, 170)

# X-Achse auf 0–50 Sekunden
ax1.set_xlim(0, 50)

# Break-Markierungen
d = .015
kwargs = dict(transform=ax1.transAxes, color='k', clip_on=False)
ax1.plot((-d,+d), (-d,+d), **kwargs)
ax1.plot((1-d,1+d), (-d,+d), **kwargs)

kwargs.update(transform=ax2.transAxes)
ax2.plot((-d,+d), (1-d,1+d), **kwargs)
ax2.plot((1-d,1+d), (1-d,1+d), **kwargs)

ax2.set_xlabel("Time (s)")
ax1.set_ylabel("Events/s")
ax2.set_ylabel("Events/s")
ax1.legend()

fig.suptitle("Throughput (Overload, 0–50 s)")
fig.tight_layout()
fig.savefig(os.path.join(outdir, "throughput_overload_brokenY_0_50s.png"), dpi=150)
plt.close()

# === Plot: Throughput (0–50 s, Y ungebrochen) ===
import numpy as np

outdir = "plots"
os.makedirs(outdir, exist_ok=True)

# nur die ersten 50 Sekunden plotten (schneller u. klarer als nur xlim)
mask = seconds <= 50
sec50 = seconds[mask]
proc50 = processed[mask]
gen50  = generated[mask]

plt.figure(figsize=(8,4))
plt.plot(sec50, proc50, lw=1.8, label="heiDPI processed (watcher_ts)")
plt.plot(sec50, gen50,  ls="--", lw=1.5, label="Generator (generator_ts) – Referenz")

plt.xlabel("Time (s)")
plt.ylabel("Events per second")
plt.title("Throughput (Overload): 0–50 s, Y-Achse ungebrochen")
plt.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(outdir, "throughput_overload_0_50s.png"), dpi=150)
plt.close()

# === Plot: Throughput processed vs. generated (Overload) ===
import matplotlib.pyplot as plt
outdir = "plots"
os.makedirs(outdir, exist_ok=True)

# Sekundenskala & Werte (bereits aligniert)
seconds   = thru_done.index.values
processed = thru_done["processed_events"].values
generated = rate_gen["generated_events"].values

plt.figure(figsize=(8,4))
plt.plot(seconds, processed, lw=1.8, label="heiDPI processed")
plt.plot(seconds, generated, ls="--", lw=1.5, label="Generator")

plt.xlabel("Time (s)")
plt.ylabel("Events per second")
plt.title("Throughput (IDLE-BURST szenario)")
plt.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(outdir, "throughput_processed_vs_generated_idle-burst.png"), dpi=150)
plt.close()

# === Plot: Memory usage (heiDPI only) ===
outdir = "plots"
os.makedirs(outdir, exist_ok=True)

mem_rows = []
for p in files:
    first_res_ts = None
    with open(p) as f:
        for line in f:
            rec = json.loads(line)
            if "packet_id" in rec:
                continue  # nur Ressourcen-Zeilen
            ts = rec.get("timestamp")
            logger_mem = rec.get("logger_memory")
            if ts is None or logger_mem is None:
                continue
            if first_res_ts is None:
                first_res_ts = ts
            sec = int((ts - first_res_ts) / 1e6)
            mem_rows.append({
                "second": sec,
                "logger_mem": float(logger_mem) / 1024.0  # MB
            })

if not mem_rows:
    print("Keine heiDPI Memory-Daten gefunden.")
else:
    mem_df = pd.DataFrame(mem_rows)
    mem_agg = (mem_df.groupby("second")
               .agg(mean_mem=("logger_mem","mean"),
                    min_mem=("logger_mem","min"),
                    max_mem=("logger_mem","max"))
               .reset_index())

    plt.figure(figsize=(8,4))
    plt.plot(mem_agg["second"], mem_agg["mean_mem"], lw=1.8, color="tab:blue", label="heiDPI memory")
#    plt.fill_between(mem_agg["second"], mem_agg["min_mem"], mem_agg["max_mem"], color="tab:blue", alpha=0.15,
#                     label="heiDPI memory (min–max)")
    plt.xlabel("Time (s)")
    plt.ylabel("Memory usage (MB)")
    plt.title("heiDPI Memory usage IDLE-BURST szenario")
    plt.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "heidpi_memory_usage_idle-burst.png"), dpi=150)
    plt.close()


# Kleine Zusammenfassung
print("\n==== Zusammenfassung ====")
print(f"Gesamtdauer (s):               {end_s}")
print(f"Summe erzeugt (Events):        {int(cum_gen.iloc[-1])}")
print(f"Summe verarbeitet (Events):    {int(cum_done.iloc[-1])}")
print(f"Max. Backlog (Events):         {int(backlog.max())} @ Sekunde {int(backlog.idxmax())}")
print(f"Backlog am Ende (Events):      {int(backlog.iloc[-1])}")

