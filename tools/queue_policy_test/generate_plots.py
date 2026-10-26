#!/usr/bin/env python3
"""Generates conference-ready latency and queueing graphs from Nighthawk and Envoy outputs."""

import argparse
import json
import os
import re
import matplotlib.pyplot as plt
import numpy as np
import yaml


# Configures Matplotlib global styling for conference slide readability.
def configure_plot_style():
    style = "seaborn-v0_8-whitegrid"
    if style in plt.style.available:
        plt.style.use(style)
    plt.rcParams["font.family"] = "sans-serif"
    plt.rcParams["font.size"] = 11
    plt.rcParams["axes.titlesize"] = 13
    plt.rcParams["axes.labelsize"] = 11
    plt.rcParams["xtick.labelsize"] = 10
    plt.rcParams["ytick.labelsize"] = 10
    plt.rcParams["legend.fontsize"] = 9.5


# Saves a Matplotlib figure as both 300-DPI PNG and vector PDF.
def save_figure(fig, output_dir, basename):
    png_path = os.path.join(output_dir, f"{basename}.png")
    pdf_path = os.path.join(output_dir, f"{basename}.pdf")
    fig.savefig(png_path, dpi=300, bbox_inches="tight", facecolor="white")
    fig.savefig(pdf_path, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"[PLOT] Saved {png_path} and {pdf_path}")


# Parses duration strings from Nighthawk JSON (such as "0.012345s") into milliseconds.
def parse_duration_ms(dur_str):
    if not dur_str:
        return 0.0
    return float(str(dur_str).replace("s", "")) * 1000.0


# Loads a Nighthawk client JSON output file, stripping any leading log lines.
def load_nighthawk_json(filepath):
    if not os.path.exists(filepath):
        return None
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    clean_text = re.sub(r"\[\d{2}:\d{2}:\d{2}\.\d+\].*(\r?\n)?", "", text)
    idx = clean_text.find("{")
    if idx == -1:
        return None
    try:
        return json.loads(clean_text[idx:])
    except Exception:
        return None


# Extracts CDF arrays and key percentiles (in ms) from a Nighthawk client JSON file.
def parse_nighthawk_client_metrics(filepath):
    target_pcts = [0.50, 0.90, 0.99, 0.999]
    empty = {
        "cdf_pct": np.array([]),
        "cdf_ms": np.array([]),
        "percentiles_ms": {p: 0.0 for p in target_pcts},
        "mean_ms": 0.0,
        "count": 0,
    }
    data = load_nighthawk_json(filepath)
    if not data:
        return empty

    for res in data.get("results", []):
        if res.get("name") != "global":
            continue
        for stat in res.get("statistics", []):
            if stat.get("id") not in (
                "benchmark_http_client.request_to_response",
                "benchmark_http_client.latency_2xx",
            ):
                continue
            pct_entries = stat.get("percentiles", [])
            if not pct_entries:
                continue
            pcts = [float(p.get("percentile", 0.0)) * 100.0 for p in pct_entries]
            durs = [parse_duration_ms(p.get("duration", "0s")) for p in pct_entries]
            pct_map = {}
            for tp in target_pcts:
                closest_idx = min(
                    range(len(pct_entries)),
                    key=lambda i: abs(float(pct_entries[i].get("percentile", 0.0)) - tp),
                )
                pct_map[tp] = round(durs[closest_idx], 2)
            return {
                "cdf_pct": np.array(pcts),
                "cdf_ms": np.array(durs),
                "percentiles_ms": pct_map,
                "mean_ms": round(parse_duration_ms(stat.get("mean", "0s")), 2),
                "count": int(stat.get("count", 0)),
            }
    return empty


# Loads all Nighthawk client metrics and Envoy timeseries data for every configured policy.
def load_all_results(scenario, results_dir):
    results = {}
    for policy in scenario["policies"]:
        pid = policy["id"]
        ts_path = os.path.join(results_dir, f"{pid}_timeseries.json")
        ts_data = {"stats_timeseries": [], "latency_timeseries": []}
        if os.path.exists(ts_path):
            with open(ts_path, "r", encoding="utf-8") as f:
                ts_data = json.load(f)

        clients_data = {}
        for client in scenario["clients"]:
            cid = client["id"]
            nh_path = os.path.join(results_dir, f"{pid}_{cid}.json")
            clients_data[cid] = parse_nighthawk_client_metrics(nh_path)

        results[pid] = {
            "policy": policy,
            "timeseries": ts_data,
            "clients": clients_data,
        }
    return results


# Determines the burst window (start_sec, end_sec) of the noisy neighbor client if present.
def get_noisy_window(scenario):
    for client in scenario["clients"]:
        if client.get("role") == "noisy":
            start = float(client.get("start_delay_sec", 0.0))
            end = start + float(client.get("duration_sec", scenario["duration_sec"]))
            return start, end
    return None


# Shades the noisy-neighbor burst window on a timeseries Axes object.
def shade_noisy_window(ax, noisy_window):
    if not noisy_window:
        return
    start, end = noisy_window
    ax.axvspan(start, end, color="#fdedec", alpha=0.55, label="Noisy Burst Window")
    ax.axvline(start, color="#c0392b", linestyle=":", linewidth=1.2, alpha=0.8)
    ax.axvline(end, color="#c0392b", linestyle=":", linewidth=1.2, alpha=0.8)


# Plots the client-observed latency CDF for a single policy onto the provided Axes.
def plot_policy_cdf_on_ax(ax, scenario, policy_res, title):
    max_lat = 1.0
    for client in scenario["clients"]:
        cid = client["id"]
        c_metrics = policy_res["clients"][cid]
        pcts, durs = c_metrics["cdf_pct"], c_metrics["cdf_ms"]
        if len(pcts) == 0:
            continue
        mask = pcts <= 99.5
        p_filt, d_filt = pcts[mask], durs[mask]
        if len(d_filt) == 0:
            continue
        max_lat = max(max_lat, float(np.max(d_filt)))
        is_noisy = client.get("role") == "noisy"
        lw = 2.6 if is_noisy else 2.2
        ls = "--" if is_noisy else "-"
        ax.plot(
            p_filt,
            d_filt,
            color=client.get("color", "#2874a6"),
            linestyle=ls,
            linewidth=lw,
            label=f"{client['label']} (P99={c_metrics['percentiles_ms'][0.99]:.1f}ms)",
        )

    ax.set_yscale("log")
    ax.set_xlim(0, 99.5)
    cur_top = ax.get_ylim()[1] if ax.get_ylim()[1] > 10.0 else 1.0
    ax.set_ylim(bottom=1.0, top=max(cur_top, max_lat * 1.6))
    ax.set_xlabel("Cumulative Percentile (%)", fontweight="bold")
    ax.set_ylabel("Client-Observed Latency (ms, log)", fontweight="bold")
    ax.set_title(title, fontweight="bold")
    ax.grid(True, which="major", alpha=0.35)
    ax.grid(True, which="minor", alpha=0.15, linestyle=":")
    ax.legend(loc="upper left", frameon=True, facecolor="white")


# Generates standalone per-policy side-by-side latency CDF panels.
def generate_cdf_by_policy_plot(scenario, all_results, output_dir):
    policies = scenario["policies"]
    n_pol = len(policies)
    fig, axes = plt.subplots(
        1, n_pol, figsize=(7.5 * n_pol, 5.8), squeeze=False, sharey=True, constrained_layout=True
    )
    for idx, pol in enumerate(policies):
        pid = pol["id"]
        plot_policy_cdf_on_ax(
            axes[0, idx],
            scenario,
            all_results[pid],
            f"{pol['label']}: Client Latency CDF (0%–99.5%)",
        )
    fig.suptitle(
        "Client-Observed Request Latency Distribution by Queue Policy",
        fontsize=15,
        fontweight="bold",
    )
    save_figure(fig, output_dir, "latency_cdf_by_policy")


# Generates cross-policy CDF overlay comparing Well-Behaved vs Noisy Neighbor clients.
def generate_cross_policy_cdf_plot(scenario, all_results, output_dir):
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.8), sharey=True, constrained_layout=True)
    line_styles = ["-", "--", "-.", ":"]
    max_lat = 1.0

    for p_idx, pol in enumerate(scenario["policies"]):
        pid = pol["id"]
        ls = line_styles[p_idx % len(line_styles)]
        for client in scenario["clients"]:
            cid = client["id"]
            c_metrics = all_results[pid]["clients"][cid]
            pcts, durs = c_metrics["cdf_pct"], c_metrics["cdf_ms"]
            if len(pcts) == 0:
                continue
            mask = pcts <= 99.5
            p_filt, d_filt = pcts[mask], durs[mask]
            if len(d_filt) == 0:
                continue
            max_lat = max(max_lat, float(np.max(d_filt)))
            target_ax = axes[1] if client.get("role") == "noisy" else axes[0]
            target_ax.plot(
                p_filt,
                d_filt,
                color=client.get("color", "#2874a6"),
                linestyle=ls,
                linewidth=2.3,
                label=f"{pol['label']} - {client['label']}",
            )

    titles = [
        "Well-Behaved Clients: Latency CDF Across Queue Policies",
        "Noisy Neighbor Client: Latency CDF Across Queue Policies",
    ]
    for idx, ax in enumerate(axes):
        ax.set_yscale("log")
        ax.set_xlim(0, 99.5)
        ax.set_ylim(bottom=1.0, top=max_lat * 1.6)
        ax.set_xlabel("Cumulative Percentile (%)", fontweight="bold")
        ax.set_ylabel("Client-Observed Latency (ms, log)", fontweight="bold")
        ax.set_title(titles[idx], fontweight="bold")
        ax.grid(True, which="major", alpha=0.35)
        ax.grid(True, which="minor", alpha=0.15, linestyle=":")
        ax.legend(loc="upper left", frameon=True, facecolor="white")

    save_figure(fig, output_dir, "latency_cdf_cross_policy")


# Plots grouped latency percentile bars for a single policy onto the provided Axes.
def plot_policy_bars_on_ax(ax, scenario, policy_res, title):
    pct_keys = [0.50, 0.90, 0.99, 0.999]
    pct_labels = ["P50", "P90", "P99", "P99.9"]
    clients = scenario["clients"]
    x = np.arange(len(pct_keys))
    width = 0.8 / max(1, len(clients))
    max_val = 1.0

    for c_idx, client in enumerate(clients):
        cid = client["id"]
        p_map = policy_res["clients"][cid]["percentiles_ms"]
        vals = [p_map.get(k, 0.0) for k in pct_keys]
        max_val = max(max_val, max(vals) if vals else 1.0)
        offset = (c_idx - (len(clients) - 1) / 2.0) * width
        bars = ax.bar(
            x + offset,
            vals,
            width=width * 0.92,
            color=client.get("color", "#2874a6"),
            edgecolor="black",
            linewidth=0.6,
            label=client["label"],
        )
        for bar in bars:
            h = bar.get_height()
            if h > 0:
                ax.annotate(
                    f"{h:.0f}",
                    xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha="center",
                    va="bottom",
                    fontsize=8,
                    fontweight="bold",
                )

    ax.set_xticks(x)
    ax.set_xticklabels(pct_labels, fontweight="bold")
    cur_top = ax.get_ylim()[1] if ax.get_ylim()[1] > 10.0 else 1.0
    ax.set_ylim(0, max(cur_top, max_val * 1.38))
    ax.set_ylabel("Client-Observed Latency (ms)", fontweight="bold")
    ax.set_title(title, fontweight="bold")
    ax.grid(True, axis="y", alpha=0.35)
    ax.legend(loc="upper left", ncol=2, frameon=True, facecolor="white")


# Generates standalone grouped bar chart of key latency percentiles (P50, P90, P99, P99.9).
def generate_percentile_bar_plot(scenario, all_results, output_dir):
    policies = scenario["policies"]
    n_pol = len(policies)
    fig, axes = plt.subplots(
        1, n_pol, figsize=(7.5 * n_pol, 5.6), squeeze=False, sharey=True, constrained_layout=True
    )
    for idx, pol in enumerate(policies):
        pid = pol["id"]
        plot_policy_bars_on_ax(
            axes[0, idx],
            scenario,
            all_results[pid],
            f"{pol['label']}: Key Latency Percentiles (ms)",
        )
    fig.suptitle(
        "Client-Observed Latency Percentiles (P50 / P90 / P99 / P99.9)",
        fontsize=15,
        fontweight="bold",
    )
    save_figure(fig, output_dir, "latency_percentiles_bar")


# Plots per-client request latency over time (1s buckets) onto the provided Axes.
def plot_latency_timeseries_on_ax(ax, scenario, policy_res, noisy_window, title):
    lat_ts = policy_res["timeseries"].get("latency_timeseries", [])
    duration = float(scenario["duration_sec"])
    shade_noisy_window(ax, noisy_window)

    max_y = 20.0
    for client in scenario["clients"]:
        cid = client["id"]
        xs, ys = [], []
        for row in lat_ts:
            t = row.get("timestamp_sec", 0.0)
            c_entry = row.get("clients", {}).get(cid, {})
            val = c_entry.get("p90_ms")
            if val is not None and t <= duration:
                xs.append(t)
                ys.append(val)
        if xs:
            max_y = max(max_y, max(ys))
            is_noisy = client.get("role") == "noisy"
            ax.plot(
                xs,
                ys,
                color=client.get("color", "#2874a6"),
                linestyle="--" if is_noisy else "-",
                linewidth=2.3 if is_noisy else 2.0,
                marker="o",
                markersize=3.5,
                label=f"{client['label']} (P90)",
            )

    ax.set_xlim(0, duration)
    cur_top = ax.get_ylim()[1] if ax.get_ylim()[1] > 10.0 else 1.0
    ax.set_ylim(0, max(cur_top, max_y * 1.25))
    ax.set_xlabel("Elapsed Time (s)", fontweight="bold")
    ax.set_ylabel("Request Latency P90 (ms)", fontweight="bold")
    ax.set_title(title, fontweight="bold")
    ax.grid(True, alpha=0.35)
    ax.legend(loc="upper left", frameon=True, facecolor="white")


# Plots Envoy pending queue depth and per-client RPS over time onto two Axes.
def plot_queue_and_rps_on_axes(ax_q, ax_rps, scenario, policy_res, noisy_window, label_prefix):
    stats_ts = policy_res["timeseries"].get("stats_timeseries", [])
    duration = float(scenario["duration_sec"])
    shade_noisy_window(ax_q, noisy_window)
    shade_noisy_window(ax_rps, noisy_window)

    ts = [row["timestamp_sec"] for row in stats_ts if row["timestamp_sec"] <= duration]
    q_depth = [row["pending_active"] for row in stats_ts if row["timestamp_sec"] <= duration]

    if ts:
        ax_q.plot(
            ts,
            q_depth,
            color="#8e44ad",
            linewidth=2.4,
            label="Pending Queue Depth (upstream_rq_pending_active)",
        )
        ax_q.fill_between(ts, 0, q_depth, color="#8e44ad", alpha=0.15)

    max_q = max(q_depth) if q_depth else 10
    ax_q.set_xlim(0, duration)
    cur_q_top = ax_q.get_ylim()[1] if ax_q.get_ylim()[1] > 5.0 else 10.0
    ax_q.set_ylim(0, max(cur_q_top, max_q * 1.2))
    ax_q.set_xlabel("Elapsed Time (s)", fontweight="bold")
    ax_q.set_ylabel("Queued Requests", fontweight="bold")
    ax_q.set_title(f"{label_prefix}: Envoy Pending Request Queue Depth", fontweight="bold")
    ax_q.grid(True, alpha=0.35)
    ax_q.legend(loc="upper left", frameon=True, facecolor="white")

    max_rps = 50.0
    for client in scenario["clients"]:
        cid = client["id"]
        c_rps = [
            row.get("client_rps", {}).get(cid, 0.0)
            for row in stats_ts
            if row["timestamp_sec"] <= duration
        ]
        if ts and c_rps:
            max_rps = max(max_rps, max(c_rps))
            is_noisy = client.get("role") == "noisy"
            ax_rps.plot(
                ts,
                c_rps,
                color=client.get("color", "#2874a6"),
                linestyle="--" if is_noisy else "-",
                linewidth=2.2,
                label=client["label"],
            )

    ax_rps.set_xlim(0, duration)
    cur_rps_top = ax_rps.get_ylim()[1] if ax_rps.get_ylim()[1] > 10.0 else 50.0
    ax_rps.set_ylim(0, max(cur_rps_top, max_rps * 1.2))
    ax_rps.set_xlabel("Elapsed Time (s)", fontweight="bold")
    ax_rps.set_ylabel("Completed Throughput (RPS)", fontweight="bold")
    ax_rps.set_title(f"{label_prefix}: Per-Client Throughput Over Time", fontweight="bold")
    ax_rps.grid(True, alpha=0.35)
    ax_rps.legend(loc="upper left", frameon=True, facecolor="white")


# Generates the 3-row timeseries figure (Latency Over Time, Queue Depth, Throughput).
def generate_timeseries_plot(scenario, all_results, output_dir):
    policies = scenario["policies"]
    n_pol = len(policies)
    noisy_window = get_noisy_window(scenario)
    fig, axes = plt.subplots(
        3,
        n_pol,
        figsize=(8.0 * n_pol, 11.5),
        squeeze=False,
        sharex=True,
        sharey="row",
        constrained_layout=True,
    )

    for idx, pol in enumerate(policies):
        pid = pol["id"]
        lbl = pol["label"]
        plot_latency_timeseries_on_ax(
            axes[0, idx],
            scenario,
            all_results[pid],
            noisy_window,
            f"{lbl}: Client Request Latency (P90) Over Time",
        )
        plot_queue_and_rps_on_axes(
            axes[1, idx], axes[2, idx], scenario, all_results[pid], noisy_window, lbl
        )

    fig.suptitle(
        "Staged Noisy-Neighbor Burst: Latency, Envoy Queue Depth, and Throughput Over Time",
        fontsize=15,
        fontweight="bold",
    )
    save_figure(fig, output_dir, "queue_and_latency_timeseries")


# Generates the combined 4-panel conference summary dashboard.
def generate_summary_dashboard(scenario, all_results, output_dir):
    policies = scenario["policies"]
    n_pol = len(policies)
    noisy_window = get_noisy_window(scenario)

    fig, axes = plt.subplots(
        4, n_pol, figsize=(8.5 * n_pol, 16.0), squeeze=False, sharey="row", constrained_layout=True
    )
    for idx, pol in enumerate(policies):
        pid = pol["id"]
        lbl = pol["label"]
        res = all_results[pid]
        plot_policy_cdf_on_ax(axes[0, idx], scenario, res, f"1. {lbl}: Client Latency CDF (0%–99.5%)")
        plot_policy_bars_on_ax(axes[1, idx], scenario, res, f"2. {lbl}: Key Latency Percentiles")
        plot_latency_timeseries_on_ax(
            axes[2, idx], scenario, res, noisy_window, f"3. {lbl}: Per-Client P90 Latency Over Time"
        )
        plot_queue_and_rps_on_axes(
            axes[3, idx],
            plt.figure().add_subplot(111),
            scenario,
            res,
            noisy_window,
            f"4. {lbl}",
        )
        plt.close(plt.gcf())

    fig.suptitle(
        "Envoy Queue Policy Benchmark Summary: Client Latencies & Queueing Behavior",
        fontsize=16,
        fontweight="bold",
    )
    save_figure(fig, output_dir, "queue_policy_summary_dashboard")


# Main entry point to parse benchmark outputs and render all conference figures.
def main():
    parser = argparse.ArgumentParser(description="Generate queue policy latency plots.")
    parser.add_argument("--config", required=True, help="Path to scenario.yaml.")
    parser.add_argument("--results-dir", required=True, help="Directory with benchmark JSON files.")
    args = parser.parse_args()

    configure_plot_style()
    with open(args.config, "r", encoding="utf-8") as f:
        scenario = yaml.safe_load(f)

    all_results = load_all_results(scenario, args.results_dir)
    generate_cdf_by_policy_plot(scenario, all_results, args.results_dir)
    generate_percentile_bar_plot(scenario, all_results, args.results_dir)
    generate_cross_policy_cdf_plot(scenario, all_results, args.results_dir)
    generate_timeseries_plot(scenario, all_results, args.results_dir)
    generate_summary_dashboard(scenario, all_results, args.results_dir)


if __name__ == "__main__":
    main()
