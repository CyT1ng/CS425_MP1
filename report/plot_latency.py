#!/usr/bin/env python3
"""Latency plot for the MP1 report.

    python3 report/plot_latency.py report/data/latency.csv

Produces report/latency.pdf (vector, for embedding) and report/latency.png.

The spec is unusually specific about this figure, and past classes have lost
points on it, so the rules are baked in here rather than left to taste:

  * average AND standard deviation, on ONE plot, with SD as error bars.
    "Do not plot SD as a separate plot from average." Never split these.
  * at least 5 trials per data point -- asserted below, not assumed.
  * discuss the plot in the report; do not just paste it.

Design choices, so you can defend them:
  * Bar chart: one measure (latency) across three nominal conditions.
  * ONE color for all three bars. The bars are a single series -- the x-axis
    labels carry identity, so a per-bar color ramp would double-encode height
    as hue and burn a channel for nothing.
  * Bars anchored at zero. A truncated bar baseline exaggerates differences and
    is the classic way to make a chart lie.
  * Direct value labels above each bar, so the figure survives being printed
    in grayscale.
"""
import csv
import statistics
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Ink and surface tokens; recessive chrome, one accent for the data.
SURFACE   = "#fcfcfb"
INK       = "#0b0b0b"
INK_SOFT  = "#52514e"
MUTED     = "#898781"
GRID      = "#e1e0d9"
BASELINE  = "#c3c2b7"
SERIES    = "#2a78d6"

ORDER  = ["rare", "infrequent", "frequent"]
LABELS = {
    # TODO: replace the match counts with your measured numbers. Naming the
    # actual selectivity is what makes the x-axis meaningful.
    "rare":       "Rare\n(~10 matches)",
    "infrequent": "Infrequent\n(~2.4K matches)",
    "frequent":   "Frequent\n(~240K matches)",
}


def load(path):
    trials = defaultdict(list)
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            if row["latency_ms"] == "NA":
                continue
            trials[row["query_class"]].append(float(row["latency_ms"]))
    return trials


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "report/data/latency.csv"
    trials = load(path)

    classes = [c for c in ORDER if c in trials]
    if not classes:
        sys.exit(f"no usable rows in {path}")

    for c in classes:
        n = len(trials[c])
        if n < 5:
            sys.exit(f"'{c}' has only {n} trials; the spec requires at least 5")

    means = [statistics.mean(trials[c]) for c in classes]
    # Sample SD (n-1). With 5-7 trials the population SD understates the spread.
    sds = [statistics.stdev(trials[c]) if len(trials[c]) > 1 else 0.0
           for c in classes]

    fig, ax = plt.subplots(figsize=(5.6, 3.3), dpi=200)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    x = range(len(classes))
    ax.bar(x, means, width=0.55, color=SERIES, zorder=3,
           yerr=sds, capsize=5,
           error_kw=dict(ecolor=INK, elinewidth=1.4, capthick=1.4, zorder=4))

    # Value labels above the error bar, not inside the bar, so a short bar's
    # label is never clipped.
    for xi, m, s in zip(x, means, sds):
        ax.text(xi, m + s + max(means) * 0.04, f"{m:.0f} ms",
                ha="center", va="bottom", fontsize=9, color=INK_SOFT, zorder=5)

    ax.set_xticks(list(x))
    ax.set_xticklabels([LABELS.get(c, c) for c in classes], fontsize=9)
    ax.set_ylabel("Query latency (ms)", fontsize=9, color=INK_SOFT)
    ax.set_title("Distributed grep latency, 4 machines x 60 MB logs\n"
                 f"mean +/- SD, n={len(trials[classes[0]])} trials",
                 fontsize=10, color=INK, pad=10)

    ax.set_ylim(0, max(m + s for m, s in zip(means, sds)) * 1.22)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    ax.xaxis.grid(False)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.spines["left"].set_color(BASELINE)
    ax.spines["bottom"].set_color(BASELINE)
    ax.tick_params(colors=MUTED, labelsize=9, length=0)
    for lbl in ax.get_xticklabels():
        lbl.set_color(INK_SOFT)

    fig.tight_layout()
    fig.savefig("report/latency.pdf", facecolor=SURFACE)
    fig.savefig("report/latency.png", facecolor=SURFACE)
    print("wrote report/latency.pdf and report/latency.png")

    print("\nnumbers for the writeup:")
    for c, m, s in zip(classes, means, sds):
        print(f"  {c:<12} {m:8.1f} ms +/- {s:5.1f}  (n={len(trials[c])})")


if __name__ == "__main__":
    main()
