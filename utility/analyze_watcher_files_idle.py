import os
import re
import json
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# ===================== Einstellungen =====================

basedir = "../benchmark_results"   # Ordner mit .json.watch-Dateien
include_pattern = None   # z.B. r"idle" oder r"ramp-python" → nur diese Dateien laden
memory_source = "logger" # "logger" (Prozess) oder "total" (System)

# ===================== Parser =====================

def parse_watch_file(filepath):
    latencies, resources = [], []
    fname = os.path.basename(filepath)
    lang = 'Python' if 'python' in fname.lower() else 'C++ port'

    with open(filepath) as f:
        for line in f:
            rec = json.loads(line)
            if 'packet_id' in rec:
                lat_ms = (rec['watcher_ts'] - rec['generator_ts']) / 1000.0
                latencies.append({
                    'latency_ms': lat_ms,
                    'language': lang,
                    'file': fname
                })
            else:
                cpu = rec.get('total_cpu')
                total_kb = rec.get('total_memory')
                logger_kb = rec.get('logger_memory')
                resources.append({
                    'timestamp': rec['timestamp'],
                    'total_cpu': float(cpu) if cpu is not None else np.nan,
                    'total_memory_MB': (float(total_kb)/1024.0) if total_kb else np.nan,
                    'logger_memory_MB': (float(logger_kb)/1024.0) if logger_kb else np.nan,
                    'language': lang,
                    'file': fname
                })

    lat_df = pd.DataFrame(latencies)
    res_df = pd.DataFrame(resources)
    if not res_df.empty:
        t0 = res_df['timestamp'].iloc[0]
        res_df['time_s'] = (res_df['timestamp'] - t0) / 1e6
    return lat_df, res_df

# ===================== Einlesen =====================

files = [f for f in os.listdir(basedir) if f.endswith(".json.watch")]
if include_pattern:
    rx = re.compile(include_pattern)
    files = [f for f in files if rx.search(f)]

lat_dfs, res_dfs = [], []
for fname in sorted(files):
    lat_df, res_df = parse_watch_file(os.path.join(basedir, fname))
    if not lat_df.empty: lat_dfs.append(lat_df)
    if not res_df.empty: res_dfs.append(res_df)

if not lat_dfs or not res_dfs:
    raise SystemExit("Keine passenden .json.watch-Dateien gefunden.")

lat = pd.concat(lat_dfs, ignore_index=True)
res = pd.concat(res_dfs, ignore_index=True)

sns.set(style="whitegrid")

# ===================== Hilfsfunktion =====================

def summarize_runs(df, value_col, lang):
    runs = df['file'].unique()
    series, times = [], []
    for run in runs:
        d = df[df['file']==run].sort_values('time_s').reset_index(drop=True)
        series.append(d[value_col].to_numpy(dtype=float))
        times.append(d['time_s'].to_numpy(dtype=float))
    mlen = min(len(s) for s in series)
    series = [s[:mlen] for s in series]
    times = [t[:mlen] for t in times]
    t = np.mean(times, axis=0)
    mean = np.mean(series, axis=0)
    vmin = np.min(series, axis=0)
    vmax = np.max(series, axis=0)
    return t, mean, vmin, vmax

# ===================== CPU Usage Plot =====================

plt.figure(figsize=(8,4))
for lang in ['Python','C++ port']:
    d = res[res['language']==lang]
    if d.empty: continue
    t, mean, vmin, vmax = summarize_runs(d, 'total_cpu', lang)
    color = '#1f77b4' if lang=='Python' else '#ff7f0e'
    plt.plot(t, mean, label=f'{lang.capitalize()} mean CPU (%)', color=color)
    plt.fill_between(t, vmin, vmax, color=color, alpha=0.2)
plt.xlabel('Time (s)')
plt.ylabel('CPU usage (%)')
plt.title('CPU usage over time')
plt.legend()
plt.tight_layout()
plt.savefig('cpu_usage_over_time.png', dpi=150)
plt.show()

# ===================== Memory Usage Plot =====================

mem_col = 'logger_memory_MB' if memory_source=='logger' else 'total_memory_MB'
plt.figure(figsize=(8,4))
for lang in ['Python','C++ port']:
    d = res[res['language']==lang]
    if d.empty: continue
    t, mean, vmin, vmax = summarize_runs(d, mem_col, lang)
    color = '#1f77b4' if lang=='Python' else '#ff7f0e'
    label = f"{lang.capitalize()} mean memory (MB) [{memory_source}]"
    plt.plot(t, mean, label=label, color=color)
    plt.fill_between(t, vmin, vmax, color=color, alpha=0.2)
plt.xlabel('Time (s)')
plt.ylabel('Memory usage (MB)')
plt.title('Memory usage over time')
plt.legend()
plt.tight_layout()
plt.savefig('memory_usage_over_time.png', dpi=150)
plt.show()

# ===================== Separate Boxplots =====================

def nice_boxplot(df, title, out_png, xmax=None):
    plt.figure(figsize=(8,4))
    ax = sns.boxplot(
        y='language', x='latency_ms', data=df,
        orient='h', width=0.25, whis=[5,95],
        showfliers=True,
        flierprops={'marker':'.','markersize':3,'alpha':0.6},
        color='#dddddd'
    )
    med = df['latency_ms'].median()
    ax.scatter([med],[df['language'].iloc[0]], s=40, zorder=3,
               label=f"Median: {med:.2f} ms")
    if xmax:
        ax.set_xlim(0, xmax)
    ax.set_ylabel("")
    ax.set_xlabel("Latency (ms)")
    ax.set_title(title)
    ax.legend(loc="lower right")
    plt.tight_layout()
    plt.savefig(out_png, dpi=150)
    plt.show()

lat_cpp    = lat[lat['language']=='C++ port'].copy()
lat_python = lat[lat['language']=='Python'].copy()

# Achsenbereiche getrennt anpassen
cpp_max = max(2.0, lat_cpp['latency_ms'].quantile(0.995))
py_max  = max(18.0, lat_python['latency_ms'].quantile(0.995))

lat_cpp.loc[:, 'language'] = 'C++'
lat_python.loc[:, 'language'] = 'Python'

nice_boxplot(lat_cpp,    "Latency (C++)",    "latency_boxplot_cpp.png",    xmax=cpp_max)
nice_boxplot(lat_python, "Latency (Python)", "latency_boxplot_python.png", xmax=py_max)

# ===================== Kennzahlen =====================

def p(x, q): return float(np.percentile(x, q)) if len(x) else np.nan
stats = (lat.groupby('language')['latency_ms']
         .agg(mean='mean', median='median', std='std',
              p99=lambda s: p(s,99), max='max', count='count'))
print("\nLatency summary (ms):\n", stats.round(3))
