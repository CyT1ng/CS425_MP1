`latency.example.csv` is SYNTHETIC data, committed only so you can exercise the
plotting pipeline before the VMs are up:

    python3 report/plot_latency.py report/data/latency.example.csv

Real runs go to `latency.csv` via `scripts/measure.sh`. Do not put numbers from
the example file anywhere near the report.
