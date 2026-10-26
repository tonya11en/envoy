#!/usr/bin/env python3
"""Orchestrates repeatable HTTP queue-policy benchmarks through Envoy using Nighthawk."""

import argparse
import json
import os
import signal
import subprocess
import sys
import threading
import time
import urllib.request
import yaml


# Loads and returns the parsed benchmark scenario YAML configuration.
def load_scenario(config_path):
    with open(config_path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


# Parses a "Header-Name: value" string into a (name, value) tuple.
def parse_header_pair(header_str):
    parts = header_str.split(":", 1)
    if len(parts) != 2:
        raise ValueError(f"Invalid header format '{header_str}', expected 'Name: value'.")
    return parts[0].strip().lower(), parts[1].strip()


# Builds the nighthawk_test_server bootstrap dictionary with a static delay filter.
def build_nighthawk_server_config(scenario):
    upstream = scenario["upstream"]
    port = int(upstream["port"])
    static_delay = str(upstream.get("static_delay", "0.010s"))
    body_size = int(upstream.get("response_body_size", 64))

    return {
        "static_resources": {
            "listeners": [
                {
                    "name": "upstream_backend_listener",
                    "address": {
                        "socket_address": {"address": "127.0.0.1", "port_value": port}
                    },
                    "filter_chains": [
                        {
                            "filters": [
                                {
                                    "name": "envoy.filters.network.http_connection_manager",
                                    "typed_config": {
                                        "@type": (
                                            "type.googleapis.com/envoy.extensions.filters.network"
                                            ".http_connection_manager.v3.HttpConnectionManager"
                                        ),
                                        "generate_request_id": False,
                                        "codec_type": "AUTO",
                                        "stat_prefix": "upstream_backend_http",
                                        "route_config": {
                                            "name": "backend_route",
                                            "virtual_hosts": [
                                                {
                                                    "name": "backend_service",
                                                    "domains": ["*"],
                                                    "routes": [
                                                        {
                                                            "match": {"prefix": "/"},
                                                            "direct_response": {"status": 200},
                                                        }
                                                    ],
                                                }
                                            ],
                                        },
                                        "http_filters": [
                                            {
                                                "name": "dynamic-delay",
                                                "typed_config": {
                                                    "@type": (
                                                        "type.googleapis.com/nighthawk.server"
                                                        ".DynamicDelayConfiguration"
                                                    ),
                                                    "static_delay": static_delay,
                                                },
                                            },
                                            {
                                                "name": "test-server",
                                                "typed_config": {
                                                    "@type": (
                                                        "type.googleapis.com/nighthawk.server"
                                                        ".ResponseOptions"
                                                    ),
                                                    "response_body_size": body_size,
                                                },
                                            },
                                            {
                                                "name": "envoy.filters.http.router",
                                                "typed_config": {
                                                    "@type": (
                                                        "type.googleapis.com/envoy.extensions"
                                                        ".filters.http.router.v3.Router"
                                                    ),
                                                    "dynamic_stats": False,
                                                },
                                            },
                                        ],
                                    },
                                }
                            ]
                        }
                    ],
                }
            ]
        },
        "admin": {
            "access_log_path": "/dev/null",
            "address": {"socket_address": {"address": "127.0.0.1", "port_value": port + 100}},
        },
    }


# Builds VirtualCluster definitions from the configured client headers for per-client Envoy stats.
def build_virtual_clusters(clients):
    vclusters = []
    for client in clients:
        headers = client.get("headers", [])
        if not headers:
            continue
        hdr_name, hdr_val = parse_header_pair(headers[0])
        vclusters.append(
            {
                "name": client["id"],
                "headers": [
                    {
                        "name": hdr_name,
                        "string_match": {"exact": hdr_val},
                    }
                ],
            }
        )
    return vclusters


# Builds the Envoy bootstrap config for a specific queue policy under test.
def build_envoy_config(scenario, policy_cfg, access_log_path):
    envoy_cfg = scenario["envoy"]
    upstream_cfg = scenario["upstream"]
    vclusters = build_virtual_clusters(scenario["clients"])

    cluster_def = {
        "name": "backend_cluster",
        "connect_timeout": "5s",
        "type": "STATIC",
        "lb_policy": "ROUND_ROBIN",
        "circuit_breakers": {
            "thresholds": [
                {
                    "priority": "DEFAULT",
                    "max_connections": int(upstream_cfg["max_connections"]),
                    "max_pending_requests": int(
                        policy_cfg.get("max_pending_requests", upstream_cfg["max_pending_requests"])
                    ),
                    "max_requests": int(upstream_cfg["max_requests"]),
                }
            ]
        },
        "load_assignment": {
            "cluster_name": "backend_cluster",
            "endpoints": [
                {
                    "lb_endpoints": [
                        {
                            "endpoint": {
                                "address": {
                                    "socket_address": {
                                        "address": "127.0.0.1",
                                        "port_value": int(upstream_cfg["port"]),
                                    }
                                }
                            }
                        }
                    ]
                }
            ],
        },
    }
    if policy_cfg.get("queuing_policy"):
        cluster_def["queuing_policies"] = {"pending_rq_policy": policy_cfg["queuing_policy"]}

    return {
        "static_resources": {
            "listeners": [
                {
                    "name": "downstream_http_listener",
                    "address": {
                        "socket_address": {
                            "address": "127.0.0.1",
                            "port_value": int(envoy_cfg["listener_port"]),
                        }
                    },
                    "filter_chains": [
                        {
                            "filters": [
                                {
                                    "name": "envoy.filters.network.http_connection_manager",
                                    "typed_config": {
                                        "@type": (
                                            "type.googleapis.com/envoy.extensions.filters.network"
                                            ".http_connection_manager.v3.HttpConnectionManager"
                                        ),
                                        "stat_prefix": "ingress_http",
                                        "codec_type": "AUTO",
                                        "access_log": [
                                            {
                                                "name": "envoy.access_loggers.file",
                                                "typed_config": {
                                                    "@type": (
                                                        "type.googleapis.com/envoy.extensions"
                                                        ".access_loggers.file.v3.FileAccessLog"
                                                    ),
                                                    "path": access_log_path,
                                                    "log_format": {
                                                        "json_format": {
                                                            "ts": "%START_TIME(%s.%3f)%",
                                                            "client": "%REQ(X-CLIENT-ID)%",
                                                            "dur_ms": "%DURATION%",
                                                            "code": "%RESPONSE_CODE%",
                                                        }
                                                    },
                                                },
                                            }
                                        ],
                                        "route_config": {
                                            "name": "local_route",
                                            "virtual_hosts": [
                                                {
                                                    "name": "backend_vhost",
                                                    "domains": ["*"],
                                                    "virtual_clusters": vclusters,
                                                    "routes": [
                                                        {
                                                            "match": {"prefix": "/"},
                                                            "route": {
                                                                "cluster": "backend_cluster",
                                                                "timeout": "30s",
                                                            },
                                                        }
                                                    ],
                                                }
                                            ],
                                        },
                                        "http_filters": [
                                            {
                                                "name": "envoy.filters.http.header_to_metadata",
                                                "typed_config": {
                                                    "@type": (
                                                        "type.googleapis.com/envoy.extensions"
                                                        ".filters.http.header_to_metadata.v3.Config"
                                                    ),
                                                    "request_rules": [
                                                        {
                                                            "header": "x-client-id",
                                                            "on_header_present": {
                                                                "metadata_namespace": (
                                                                    "envoy.filters.http"
                                                                    ".header_to_metadata"
                                                                ),
                                                                "key": "client_id",
                                                                "type": "STRING",
                                                            },
                                                        },
                                                        {
                                                            "header": "x-tenant-tier",
                                                            "on_header_present": {
                                                                "metadata_namespace": (
                                                                    "envoy.filters.http"
                                                                    ".header_to_metadata"
                                                                ),
                                                                "key": "tenant_tier",
                                                                "type": "STRING",
                                                            },
                                                        },
                                                    ],
                                                },
                                            },
                                            {
                                                "name": "envoy.filters.http.router",
                                                "typed_config": {
                                                    "@type": (
                                                        "type.googleapis.com/envoy.extensions"
                                                        ".filters.http.router.v3.Router"
                                                    ),
                                                    "dynamic_stats": True,
                                                },
                                            },
                                        ],
                                    },
                                }
                            ]
                        }
                    ],
                }
            ],
            "clusters": [cluster_def],
        },
        "admin": {
            "access_log_path": "/dev/null",
            "address": {
                "socket_address": {
                    "address": "127.0.0.1",
                    "port_value": int(envoy_cfg["admin_port"]),
                }
            },
        },
    }


# Fetches JSON stats from the Envoy admin endpoint, returning None on error.
def fetch_envoy_stats(admin_url):
    try:
        req = urllib.request.Request(f"{admin_url}/stats?format=json")
        with urllib.request.urlopen(req, timeout=0.8) as resp:
            if resp.status == 200:
                return json.loads(resp.read().decode("utf-8"))
    except Exception:
        return None
    return None


# Extracts queue depth and per-client request counters from an Envoy stats snapshot.
def extract_stats_snapshot(stats_json, client_ids):
    snap = {
        "pending_active": 0,
        "upstream_rq_active": 0,
        "pending_overflow": 0,
        "queue_overloaded": 0,
        "client_rq_total": {cid: 0 for cid in client_ids},
    }
    if not stats_json or "stats" not in stats_json:
        return snap

    for entry in stats_json["stats"]:
        name = entry.get("name", "")
        val = entry.get("value", 0)
        if name == "cluster.backend_cluster.upstream_rq_pending_active":
            snap["pending_active"] = int(val)
        elif name == "cluster.backend_cluster.upstream_rq_active":
            snap["upstream_rq_active"] = int(val)
        elif name == "cluster.backend_cluster.upstream_rq_pending_overflow":
            snap["pending_overflow"] = int(val)
        elif name == "cluster.backend_cluster.upstream_queue_overloaded":
            snap["queue_overloaded"] = int(val)
        else:
            for cid in client_ids:
                prefix = f"vhost.backend_vhost.vcluster.{cid}.upstream_rq_total"
                if name == prefix:
                    snap["client_rq_total"][cid] = int(val)
    return snap


# Continuously samples Envoy stats during the test phase and stores snapshots in out_list.
def sample_stats_worker(admin_url, duration_sec, interval_sec, client_ids, stop_event, out_list):
    start_ts = time.time()
    prev_snap = None
    prev_t = 0.0

    while not stop_event.is_set():
        elapsed = time.time() - start_ts
        if elapsed > duration_sec + 1.0:
            break
        raw = fetch_envoy_stats(admin_url)
        if raw is not None:
            snap = extract_stats_snapshot(raw, client_ids)
            dt = max(0.05, elapsed - prev_t) if prev_snap is not None else interval_sec
            client_rps = {}
            for cid in client_ids:
                if prev_snap is None:
                    client_rps[cid] = 0.0
                else:
                    delta = snap["client_rq_total"][cid] - prev_snap["client_rq_total"][cid]
                    client_rps[cid] = round(max(0.0, delta / dt), 2)
            out_list.append(
                {
                    "timestamp_sec": round(elapsed, 2),
                    "pending_active": snap["pending_active"],
                    "upstream_rq_active": snap["upstream_rq_active"],
                    "pending_overflow": snap["pending_overflow"],
                    "queue_overloaded": snap["queue_overloaded"],
                    "client_rps": client_rps,
                }
            )
            prev_snap = snap
            prev_t = elapsed
        time.sleep(interval_sec)


# Parses Envoy's per-request JSON access log into 1-second bucketed latency quantiles per client.
def parse_access_log_timeseries(access_log_path, client_ids, duration_sec):
    if not os.path.exists(access_log_path):
        return []

    records = []
    with open(access_log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                ts = float(obj.get("ts", 0.0))
                cid = str(obj.get("client", ""))
                dur = float(obj.get("dur_ms", 0.0))
                if cid in client_ids and ts > 0:
                    records.append((ts, cid, dur))
            except Exception:
                continue

    if not records:
        return []

    min_ts = min(r[0] for r in records)
    num_buckets = int(duration_sec) + 1
    buckets = [{cid: [] for cid in client_ids} for _ in range(num_buckets)]

    for ts, cid, dur in records:
        sec_idx = int(ts - min_ts)
        if 0 <= sec_idx < num_buckets:
            buckets[sec_idx][cid].append(dur)

    result = []
    for sec_idx in range(num_buckets):
        entry = {"timestamp_sec": float(sec_idx), "clients": {}}
        for cid in client_ids:
            vals = sorted(buckets[sec_idx][cid])
            if vals:
                n = len(vals)
                p50 = vals[min(n - 1, int(0.50 * n))]
                p90 = vals[min(n - 1, int(0.90 * n))]
                p99 = vals[min(n - 1, int(0.99 * n))]
                mean = sum(vals) / n
                entry["clients"][cid] = {
                    "count": n,
                    "mean_ms": round(mean, 2),
                    "p50_ms": round(p50, 2),
                    "p90_ms": round(p90, 2),
                    "p99_ms": round(p99, 2),
                }
            else:
                entry["clients"][cid] = {
                    "count": 0,
                    "mean_ms": None,
                    "p50_ms": None,
                    "p90_ms": None,
                    "p99_ms": None,
                }
        result.append(entry)
    return result


# Builds the nighthawk_client shell command string for a single synthetic client.
def build_client_cmd(nh_client_bin, listener_port, client_cfg):
    rps = int(client_cfg["rps"])
    connections = int(client_cfg.get("connections", 60))
    concurrency = int(client_cfg.get("concurrency", 1))
    duration = int(client_cfg["duration_sec"])

    cmd_parts = [
        nh_client_bin,
        f"--concurrency {concurrency}",
        f"--rps {rps}",
        f"--connections {connections}",
        f"--duration {duration}",
        "--prefetch-connections",
        "--no-default-failure-predicates",
        "--output-format json",
    ]
    if client_cfg.get("request_body_size"):
        cmd_parts.append(f"--request-body-size {int(client_cfg['request_body_size'])}")
    for hdr in client_cfg.get("headers", []):
        cmd_parts.append(f'--request-header "{hdr}"')

    cmd_parts.append(f"http://127.0.0.1:{listener_port}/")
    return " ".join(cmd_parts)


# Cleans up any lingering processes on the benchmark ports or Nighthawk containers.
def cleanup_ports_and_containers(ports):
    port_args = " ".join(f"{p}/tcp" for p in ports)
    subprocess.run(f"fuser -k {port_args} 2>/dev/null || true", shell=True, check=False)
    subprocess.run(
        "docker ps -q --filter ancestor=envoyproxy/nighthawk-dev:latest | "
        "xargs -r docker rm -f 2>/dev/null || true",
        shell=True,
        check=False,
    )
    time.sleep(1.0)


# Waits until an HTTP endpoint responds with status 200 or raises TimeoutError.
def wait_for_http_ready(url, timeout_sec=15.0):
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=0.5) as resp:
                if resp.status == 200:
                    return
        except Exception:
            time.sleep(0.2)
    raise TimeoutError(f"Timed out waiting for {url} to become ready.")


# Sends a brief warmup burst so Envoy establishes all upstream connections before t=0.
def run_warmup(nh_client_bin, listener_port, max_connections):
    warmup_cmd = (
        f"{nh_client_bin} --concurrency 1 --rps 50 --connections {max_connections * 2} "
        f"--duration 2 --prefetch-connections --no-default-failure-predicates "
        f"http://127.0.0.1:{listener_port}/ >/dev/null 2>&1"
    )
    subprocess.run(warmup_cmd, shell=True, check=False)
    time.sleep(0.5)


# Executes a full benchmark phase for a single queue policy configuration.
def run_policy_phase(scenario, policy_cfg, output_dir, envoy_bin, nh_client_bin, nh_server_bin):
    policy_id = policy_cfg["id"]
    print(f"\n{'=' * 74}\n Starting Benchmark Phase: {policy_cfg['label']} ({policy_id})\n{'=' * 74}")

    envoy_port = int(scenario["envoy"]["listener_port"])
    admin_port = int(scenario["envoy"]["admin_port"])
    upstream_port = int(scenario["upstream"]["port"])
    ports = [envoy_port, admin_port, upstream_port, upstream_port + 100]
    cleanup_ports_and_containers(ports)

    server_cfg_path = os.path.join(output_dir, f"{policy_id}_nh_server.yaml")
    envoy_cfg_path = os.path.join(output_dir, f"{policy_id}_envoy.yaml")
    access_log_path = os.path.join(output_dir, f"{policy_id}_access.log")
    if os.path.exists(access_log_path):
        os.remove(access_log_path)

    server_cfg_dict = build_nighthawk_server_config(scenario)
    with open(server_cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(server_cfg_dict, f, sort_keys=False)
    with open(envoy_cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(build_envoy_config(scenario, policy_cfg, access_log_path), f, sort_keys=False)

    server_log = open(os.path.join(output_dir, f"{policy_id}_server.log"), "w", encoding="utf-8")
    server_cfg_inline = json.dumps(server_cfg_dict)
    server_cmd = (
        f"{nh_server_bin} --concurrency 8 --use-dynamic-base-id "
        f"--config-yaml '{server_cfg_inline}'"
    )
    server_proc = subprocess.Popen(
        server_cmd, shell=True, stdout=server_log, stderr=subprocess.STDOUT, preexec_fn=os.setsid
    )

    envoy_log = open(os.path.join(output_dir, f"{policy_id}_envoy.log"), "w", encoding="utf-8")
    concurrency = int(scenario["envoy"].get("concurrency", 1))
    envoy_cmd = (
        f"{envoy_bin} -c {envoy_cfg_path} --use-dynamic-base-id "
        f"--concurrency {concurrency} --file-flush-interval-msec 100 --log-level warn"
    )
    envoy_proc = subprocess.Popen(
        envoy_cmd, shell=True, stdout=envoy_log, stderr=subprocess.STDOUT, preexec_fn=os.setsid
    )

    try:
        wait_for_http_ready(f"http://127.0.0.1:{upstream_port}/")
        wait_for_http_ready(f"http://127.0.0.1:{admin_port}/ready")
        print("[1/4] Warming up upstream connection pool...")
        run_warmup(nh_client_bin, envoy_port, int(scenario["upstream"]["max_connections"]))

        client_ids = [c["id"] for c in scenario["clients"]]
        duration_sec = float(scenario["duration_sec"])
        sample_interval = float(scenario.get("sample_interval_sec", 0.5))
        stop_event = threading.Event()
        timeseries_samples = []

        print("[2/4] Starting Envoy stats sampler...")
        sampler_thread = threading.Thread(
            target=sample_stats_worker,
            args=(
                f"http://127.0.0.1:{admin_port}",
                duration_sec,
                sample_interval,
                client_ids,
                stop_event,
                timeseries_samples,
            ),
        )
        sampler_thread.start()

        print("[3/4] Running synthetic Nighthawk HTTP clients...")
        client_procs = []
        for client_cfg in scenario["clients"]:
            cid = client_cfg["id"]
            delay = float(client_cfg.get("start_delay_sec", 0))
            cmd = build_client_cmd(nh_client_bin, envoy_port, client_cfg)
            if delay > 0:
                cmd = f"sleep {delay} && {cmd}"
            out_file = open(
                os.path.join(output_dir, f"{policy_id}_{cid}.json"), "w", encoding="utf-8"
            )
            err_file = open(
                os.path.join(output_dir, f"{policy_id}_{cid}.err"), "w", encoding="utf-8"
            )
            proc = subprocess.Popen(cmd, shell=True, stdout=out_file, stderr=err_file)
            client_procs.append((proc, out_file, err_file))

        for proc, out_file, err_file in client_procs:
            proc.wait()
            out_file.close()
            err_file.close()

        stop_event.set()
        sampler_thread.join()

        print("[4/4] Saving final stats and per-request latency timeseries...")
        final_stats = fetch_envoy_stats(f"http://127.0.0.1:{admin_port}")
        with open(os.path.join(output_dir, f"{policy_id}_stats.json"), "w", encoding="utf-8") as f:
            json.dump(final_stats or {}, f, indent=2)

        # Terminate Envoy cleanly first so its AccessLogManager flushes all buffered entries.
        try:
            os.killpg(os.getpgid(envoy_proc.pid), signal.SIGTERM)
            envoy_proc.wait(timeout=5.0)
        except Exception:
            pass

        access_ts = parse_access_log_timeseries(access_log_path, client_ids, duration_sec)
        combined_ts = {
            "policy_id": policy_id,
            "policy_label": policy_cfg["label"],
            "stats_timeseries": timeseries_samples,
            "latency_timeseries": access_ts,
        }
        ts_path = os.path.join(output_dir, f"{policy_id}_timeseries.json")
        with open(ts_path, "w", encoding="utf-8") as f:
            json.dump(combined_ts, f, indent=2)

    finally:
        for proc in (envoy_proc, server_proc):
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            except Exception:
                pass
        server_log.close()
        envoy_log.close()
        cleanup_ports_and_containers(ports)


# Refreshes latency_timeseries in existing timeseries JSON files from flushed access logs.
def refresh_timeseries_from_access_logs(scenario, output_dir):
    client_ids = [c["id"] for c in scenario["clients"]]
    duration_sec = float(scenario["duration_sec"])
    for policy_cfg in scenario["policies"]:
        pid = policy_cfg["id"]
        access_log_path = os.path.join(output_dir, f"{pid}_access.log")
        ts_path = os.path.join(output_dir, f"{pid}_timeseries.json")
        if os.path.exists(access_log_path) and os.path.exists(ts_path):
            with open(ts_path, "r", encoding="utf-8") as f:
                combined_ts = json.load(f)
            combined_ts["latency_timeseries"] = parse_access_log_timeseries(
                access_log_path, client_ids, duration_sec
            )
            with open(ts_path, "w", encoding="utf-8") as f:
                json.dump(combined_ts, f, indent=2)


# Main CLI entry point for running the benchmark and invoking the plotting script.
def main():
    parser = argparse.ArgumentParser(description="Run Envoy queue policy latency benchmark.")
    parser.add_argument("--config", required=True, help="Path to scenario.yaml.")
    parser.add_argument("--output-dir", required=True, help="Directory for results and plots.")
    parser.add_argument("--envoy-bin", required=True, help="Path to Envoy binary.")
    parser.add_argument("--nighthawk-client-bin", required=True, help="Nighthawk client command.")
    parser.add_argument("--nighthawk-server-bin", required=True, help="Nighthawk server command.")
    parser.add_argument(
        "--plot-only", action="store_true", help="Skip load generation and only regenerate plots."
    )
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    scenario = load_scenario(args.config)

    if not args.plot_only:
        for policy_cfg in scenario["policies"]:
            run_policy_phase(
                scenario,
                policy_cfg,
                args.output_dir,
                args.envoy_bin,
                args.nighthawk_client_bin,
                args.nighthawk_server_bin,
            )
    else:
        refresh_timeseries_from_access_logs(scenario, args.output_dir)

    plot_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "generate_plots.py")
    subprocess.run(
        [sys.executable, plot_script, "--config", args.config, "--results-dir", args.output_dir],
        check=True,
    )


if __name__ == "__main__":
    main()
