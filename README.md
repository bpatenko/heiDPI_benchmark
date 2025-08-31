# heiDPI Benchmark Repository

Benchmark and tooling suite for evaluating the performance of the `heiDPI` logging pipeline.  
The project compares a **Python baseline** against a **C++ port** and provides auxiliary utilities for post‑processing and visualization.

---

## Table of Contents

1. [Overview](#overview)
2. [Repository Layout](#repository-layout)
3. [Building](#building)
4. [Running the Benchmark](#running-the-benchmark)
5. [Utility Scripts](#utility-scripts)
6. [Benchmark Results](#benchmark-results)
7. [License](#license)

---

## Overview

`heiDPI` turns Deep‑Packet‑Inspection (DPI) events into structured data.  
This repository contains:

- A C++ **benchmark harness** that generates synthetic traffic and drives loggers.
- The original **Python heiDPI logger**.
- A **C++ logger port** to evaluate performance benefits.
- A collection of **utility scripts** for analysing and plotting benchmark data.

---

## Repository Layout

```
.
├── benchmark/                 # C++ benchmark harness + Python scripts
│   ├── src/                   # C++ implementation (generator, watcher, analyser …)
│   ├── include/               # C++ headers
│   ├── heiDPI_logger.py       # Python logger entry point
│   ├── heiDPI_logger_profiler.py
│   ├── heiDPI_logger_mock.py
│   └── scenarios.json         # Predefined load scenarios (IDLE, BURST, RAMP)
├── heidpi/                    # Python baseline (env, server, logger, JSON schemas)
├── heidpi_logger_cpp_port2/   # C++ logger port
│   ├── src/                   # C++ source files (Config, EventProcessor, NDPI client …)
│   └── include/               # Corresponding headers
├── utility/                   # Stand‑alone scripts & diagrams (see below)
├── benchmark_results/         # Sample output data for various scenarios
└── CMakeLists.txt             # Root build file (adds benchmark and C++ logger subprojects)
```

---

## Building

### Prerequisites
- CMake ≥ 3.10
- C++17/20 compiler with pthreads
- Python 3.9+ (for baseline logger & utilities)

### Build Steps

Build each CMake project separately.

```bash
# Build the benchmark
cd benchmark
mkdir build && cd build
cmake ..          # Fetches third‑party dependencies
cmake --build .   # Produces the benchmark binary

# Build the C++ logger port
cd ../../heidpi_logger_cpp_port2
mkdir build && cd build
cmake ..
cmake --build .   # Produces the heidpi_cpp logger
```

These steps generate two binaries:
`benchmark` (scenario driver) and `heidpi_cpp` (C++ logger).

---

## Running the Benchmark

1. **Prepare configuration**
   Edit `benchmark/config.json` (select logger type, paths, scenario file etc.).

2. **Run**
   From `benchmark/build`:

   ```bash
   ./benchmark
   ```

3. **Scenarios**  
   `benchmark/scenarios.json` defines traffic patterns:
    - `IDLE` – minimal event rate
    - `BURST` – fixed high rate for a duration
    - `RAMP` – gradually increasing event rate

---

## Utility Scripts

The `utility/` directory provides stand‑alone analysis and visualization helpers:

- `analyze_watcher_files_cpp_burst.py`, `analyze_watcher_files_ramp.py`, …  
  Parse `.watch` files produced by the benchmark, generate plots using pandas/matplotlib.
- `analyze_watcher_file_overlead_python_version.py`  
  Offers a Python-only perspective on watcher data.
- `detected_dropped_events.py`  
  Detects dropped events across runs.
- `*.puml` + `plantuml.jar`  
  PlantUML diagrams to visualize benchmark and logger sequence flows.

> **Tip:** Many utility scripts use relative paths (e.g., `../benchmark_results`). Adjust them as needed when running from different directories.

The utility suite is not tied to the academic paper and can be freely adapted for custom analyses.

---

## Benchmark Results

`benchmark_results/` contains example outputs for several scenarios (e.g., `burst_cpp_1000events`, `ramp-python`).  
These can serve as reference data for the utility scripts or for validating new builds.

---

## License

This repository does not specify a license. Check with the authors before redistributing or reusing the code.
