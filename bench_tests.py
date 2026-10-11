#!/usr/bin/env python3
"""
bench_engine.py - collects the numbers for your chess engine's resume/README.

What it measures
  1. Perft correctness + move-generation speed      (go perft N)
  2. Search speed: time per depth, nodes/sec        (go depth N)
  3. Move-ordering impact: nodes/time across builds (--compare)
  4. Playing strength vs limited Stockfish          (--strength, needs cutechess-cli)
  5. Tactics suite, e.g. Win At Chess               (--wac file.epd, needs python-chess)
  6. Lines of code                                  (--src)

Examples
  python3 bench_engine.py --engine ./chess
  python3 bench_engine.py --engine ./chess --perft-depth 5 --depths 4 5 6 7
  python3 bench_engine.py --engine ./chess \
      --compare noorder=./chess_noorder --compare mvvlva=./chess_mvvlva --cmp-depth 6
  python3 bench_engine.py --engine ./chess --strength --stockfish /usr/games/stockfish \
      --elos 1400 1600 1800 --games 100
  python3 bench_engine.py --engine ./chess --wac WAC.epd --wac-depth 6

Build with -O2/-O3 first. Results are also written to bench_results.json.

Node counts: if your engine prints UCI "info ... nodes N" lines before "bestmove",
this script uses them. If not, add a node counter and print e.g.
    info depth 6 nodes 1234567 time 812 nps 1520000
and the nodes / nps columns will fill in.
"""

import argparse
import glob
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

# --------------------------------------------------------------------------- #
# Test data
# --------------------------------------------------------------------------- #

START_FEN = None  # None = "position startpos"
KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
POSITION3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"

# Standard perft node counts (chessprogramming.org)
PERFT_SUITE = [
    ("Start position", START_FEN, {1: 20, 2: 400, 3: 8902, 4: 197281, 5: 4865609}),
    ("Kiwipete", KIWIPETE, {1: 48, 2: 2039, 3: 97862, 4: 4085603}),
    ("Endgame (pos 3)", POSITION3, {1: 14, 2: 191, 3: 2812, 4: 43238, 5: 674624}),
]

SEARCH_POSITIONS = [
    ("Start position", START_FEN),
    ("Middlegame (Kiwipete)", KIWIPETE),
    ("Endgame (pos 3)", POSITION3),
]

# Varied openings so engine-vs-engine games are not all identical (EPD: 4 fields).
OPENING_EPDS = [
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3",
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6",
    "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3",
    "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq d6",
    "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq c3",
    "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq -",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6",
    "rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
    "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -",
    "rnbqkbnr/pppppp1p/6p1/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
]


# --------------------------------------------------------------------------- #
# UCI engine wrapper
# --------------------------------------------------------------------------- #

class Engine:
    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.proc = None
        self.q = None
        self.start()

    def start(self):
        self.proc = subprocess.Popen(
            [self.path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.q = queue.Queue()

        def reader(proc, q):
            for line in proc.stdout:
                q.put(line.rstrip("\r\n"))
            q.put(None)

        threading.Thread(target=reader, args=(self.proc, self.q), daemon=True).start()
        self.send("uci")
        self.wait_for(lambda l: l.strip() == "uciok", 10)
        self.ready()

    def restart(self):
        self.kill()
        self.start()

    def send(self, cmd):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def wait_for(self, pred, timeout):
        end = time.perf_counter() + timeout
        lines = []
        while True:
            remaining = end - time.perf_counter()
            if remaining <= 0:
                raise TimeoutError("engine did not answer in time")
            try:
                line = self.q.get(timeout=remaining)
            except queue.Empty:
                raise TimeoutError("engine did not answer in time")
            if line is None:
                raise RuntimeError("engine process exited")
            lines.append(line)
            if pred(line):
                return lines

    def ready(self):
        self.send("isready")
        return self.wait_for(lambda l: l.strip() == "readyok", 10)

    def set_position(self, fen):
        self.send("position startpos" if fen is None else f"position fen {fen}")
        lines = self.ready()
        if any("invalid position" in l for l in lines):
            raise RuntimeError(f"engine rejected position: {fen or 'startpos'}")

    def new_game(self):
        self.send("ucinewgame")
        self.ready()

    def perft(self, depth, timeout):
        t0 = time.perf_counter()
        self.send(f"go perft {depth}")
        lines = self.wait_for(lambda l: l.startswith("nodes "), timeout)
        dt = time.perf_counter() - t0
        return int(lines[-1].split()[1]), dt

    def go_depth(self, depth, timeout):
        t0 = time.perf_counter()
        self.send(f"go depth {depth}")
        lines = self.wait_for(lambda l: l.startswith("bestmove"), timeout)
        dt = time.perf_counter() - t0
        nodes = None
        for l in lines:
            if l.startswith("info"):
                m = re.search(r"\bnodes\s+(\d+)", l)
                if m:
                    nodes = int(m.group(1))
        parts = lines[-1].split()
        best = parts[1] if len(parts) > 1 else "0000"
        return {"best": best, "time": dt, "nodes": nodes}

    def kill(self):
        try:
            self.proc.kill()
            self.proc.wait(timeout=2)
        except Exception:
            pass

    def quit(self):
        try:
            self.send("quit")
            self.proc.wait(timeout=2)
        except Exception:
            self.kill()


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #

def fmt_int(n):
    return f"{n:,}" if n is not None else "n/a"


def fmt_nps(nodes, secs):
    if nodes is None or secs <= 0:
        return "n/a"
    return f"{nodes / secs:,.0f}"


def table(headers, rows):
    widths = [max(len(str(x)) for x in col) for col in zip(headers, *rows)]
    line = "  ".join(h.ljust(w) for h, w in zip(headers, widths))
    print(line)
    print("  ".join("-" * w for w in widths))
    for r in rows:
        print("  ".join(str(c).ljust(w) for c, w in zip(r, widths)))
    print()


def section(title):
    print("=" * 72)
    print(title)
    print("=" * 72)


# --------------------------------------------------------------------------- #
# 1. Perft
# --------------------------------------------------------------------------- #

def run_perft(eng, max_depth, timeout):
    section("1. PERFT (move-generation correctness and speed)")
    rows, all_ok, best_nps = [], True, 0
    results = []
    for name, fen, expected in PERFT_SUITE:
        for depth in sorted(expected):
            if depth > max_depth:
                continue
            try:
                eng.set_position(fen)
                nodes, dt = eng.perft(depth, timeout)
            except TimeoutError:
                rows.append([name, depth, expected[depth], "TIMEOUT", "-", "-", "-"])
                eng.restart()
                all_ok = False
                continue
            ok = nodes == expected[depth]
            all_ok &= ok
            nps = nodes / dt if dt > 0 else 0
            if dt > 0.05:
                best_nps = max(best_nps, nps)
            rows.append([name, depth, f"{expected[depth]:,}", f"{nodes:,}",
                         "PASS" if ok else "FAIL", f"{dt:.3f}s", f"{nps:,.0f}"])
            results.append({"position": name, "depth": depth, "expected": expected[depth],
                            "got": nodes, "ok": ok, "seconds": round(dt, 4)})
    table(["Position", "Depth", "Expected", "Got", "Result", "Time", "Nodes/s"], rows)
    print("All perft checks passed." if all_ok else
          "Some perft checks FAILED: fix move generation before trusting search numbers.")
    print(f"Best perft speed (runs over 50 ms): {best_nps:,.0f} nodes/s\n")
    return {"all_ok": all_ok, "best_nps": round(best_nps), "runs": results}


# --------------------------------------------------------------------------- #
# 2. Search speed
# --------------------------------------------------------------------------- #

def run_search(eng, depths, timeout, stop_after):
    section("2. SEARCH SPEED (go depth N)")
    rows, results = [], []
    deepest_1s = {}
    total_nodes = total_time = 0.0
    have_nodes = False
    for name, fen in SEARCH_POSITIONS:
        for depth in depths:
            try:
                eng.new_game()
                eng.set_position(fen)
                r = eng.go_depth(depth, timeout)
            except TimeoutError:
                rows.append([name, depth, "TIMEOUT", "-", "-", "-"])
                eng.restart()
                break
            rows.append([name, depth, f"{r['time']:.3f}s", fmt_int(r["nodes"]),
                         fmt_nps(r["nodes"], r["time"]), r["best"]])
            results.append({"position": name, "depth": depth, "seconds": round(r["time"], 4),
                            "nodes": r["nodes"], "best": r["best"]})
            if r["time"] <= 1.0:
                deepest_1s[name] = depth
            if r["nodes"] is not None:
                have_nodes = True
                total_nodes += r["nodes"]
                total_time += r["time"]
            if r["time"] > stop_after:
                break
    table(["Position", "Depth", "Time", "Nodes", "Nodes/s", "Best move"], rows)
    print("Deepest depth finishing within 1 second:")
    for name, _ in SEARCH_POSITIONS:
        print(f"  {name}: {deepest_1s.get(name, 'less than the smallest depth tested')}")
    avg_nps = None
    if have_nodes and total_time > 0:
        avg_nps = total_nodes / total_time
        print(f"\nAverage search speed: {avg_nps:,.0f} nodes/s")
    else:
        print("\nNo 'info ... nodes N' lines seen: add a node counter to get nodes/s.")
    print()
    return {"runs": results, "avg_nps": round(avg_nps) if avg_nps else None,
            "deepest_within_1s": deepest_1s}


# --------------------------------------------------------------------------- #
# 3. Move-ordering comparison
# --------------------------------------------------------------------------- #

def run_compare(main_engine, variants, depth, timeout):
    section(f"3. MOVE-ORDERING / PRUNING COMPARISON (fixed depth {depth})")
    builds = [("full (--engine)", main_engine)] + variants
    stats = []
    for label, path in builds:
        eng = Engine(path)
        nodes, secs, have_nodes = 0, 0.0, True
        for _, fen in SEARCH_POSITIONS:
            try:
                eng.new_game()
                eng.set_position(fen)
                r = eng.go_depth(depth, timeout)
            except TimeoutError:
                print(f"  {label}: timed out; lower --cmp-depth")
                eng.restart()
                nodes, secs, have_nodes = None, None, False
                break
            secs += r["time"]
            if r["nodes"] is None:
                have_nodes = False
            else:
                nodes += r["nodes"]
        eng.quit()
        stats.append({"label": label, "nodes": nodes if have_nodes else None, "secs": secs})
    base = stats[0]
    rows = []
    for s in stats:
        node_str = fmt_int(s["nodes"])
        time_str = f"{s['secs']:.3f}s" if s["secs"] is not None else "TIMEOUT"
        if s is base or s["secs"] is None or base["secs"] is None:
            speedup = "-"
        else:
            speedup = f"{s['secs'] / base['secs']:.1f}x slower than full"
        if s is base or not (s["nodes"] and base["nodes"]):
            reduction = "-"
        else:
            reduction = f"full searches {100 * (1 - base['nodes'] / s['nodes']):.1f}% fewer nodes"
        rows.append([s["label"], node_str, time_str, speedup, reduction])
    table(["Build", "Total nodes", "Total time", "Time", "Nodes"], rows)
    print("Tip: build the variants by disabling ordering heuristics (e.g. no ordering, MVV-LVA only)\n"
          "     and keep the same depth, so differences come only from pruning efficiency.\n")
    return stats


# --------------------------------------------------------------------------- #
# 4. Strength vs limited Stockfish
# --------------------------------------------------------------------------- #

def run_strength(args):
    section("4. STRENGTH vs LIMITED STOCKFISH (cutechess-cli)")
    cutechess = args.cutechess or shutil.which("cutechess-cli")
    stockfish = args.stockfish or shutil.which("stockfish")
    if not cutechess or not stockfish:
        print("Need both cutechess-cli and stockfish (use --cutechess / --stockfish paths).\n")
        return None
    tmp = tempfile.NamedTemporaryFile("w", suffix=".epd", delete=False)
    tmp.write("\n".join(OPENING_EPDS) + "\n")
    tmp.close()
    results = []
    print("Note: your engine ignores the clock (default depth 4), so a generous time margin is used.\n")
    for elo in args.elos:
        cmd = [
            cutechess,
            "-engine", "name=Mine", f"cmd={os.path.abspath(args.engine)}",
            "-engine", "name=SF", f"cmd={stockfish}",
            "option.UCI_LimitStrength=true", f"option.UCI_Elo={elo}",
            "-each", "proto=uci", f"st={args.move_time}", "timemargin=60000",
            "-rounds", str(max(1, args.games // 2)), "-games", "2", "-repeat",
            "-concurrency", str(args.concurrency),
            "-openings", f"file={tmp.name}", "format=epd", "order=random",
            "-resign", "movecount=3", "score=800",
            "-draw", "movenumber=40", "movecount=8", "score=10",
            "-pgnout", f"games_vs_sf{elo}.pgn",
        ]
        print(f"--- Stockfish limited to {elo} Elo, {args.games} games ---")
        score = elo_line = None
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for line in proc.stdout:
            line = line.rstrip()
            if line.startswith("Score of"):
                score = line
                print("  " + line)
            elif line.startswith("Elo difference"):
                elo_line = line
        proc.wait()
        entry = {"stockfish_elo": elo, "score_line": score, "elo_line": elo_line}
        m = re.search(r"Score of Mine vs SF:\s+(\d+) - (\d+) - (\d+)", score or "")
        if m:
            entry.update(wins=int(m.group(1)), losses=int(m.group(2)), draws=int(m.group(3)))
        print(f"  {elo_line or 'no result parsed (check cutechess output)'}\n")
        results.append(entry)
    os.unlink(tmp.name)
    print("Stockfish's UCI_Elo scale is not calibrated to human ratings: describe the result as\n"
          "'roughly X Elo against Stockfish limited to N', not as an official rating.\n")
    return results


# --------------------------------------------------------------------------- #
# 5. Tactics suite (WAC etc.)
# --------------------------------------------------------------------------- #

def run_wac(eng, path, depth, timeout):
    section(f"5. TACTICS SUITE: {os.path.basename(path)} (depth {depth})")
    try:
        import chess  # pip install python-chess
    except ImportError:
        print("Install python-chess first:  pip install python-chess\n")
        return None
    solved = total = timeouts = 0
    failed = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            board = chess.Board()
            try:
                ops = board.set_epd(line)
            except ValueError:
                continue
            best_moves = [m.uci() for m in ops.get("bm", [])]
            if not best_moves:
                continue
            total += 1
            try:
                eng.new_game()
                eng.set_position(board.fen())
                r = eng.go_depth(depth, timeout)
            except TimeoutError:
                timeouts += 1
                eng.restart()
                continue
            if r["best"] in best_moves:
                solved += 1
            else:
                failed.append(ops.get("id", f"#{total}"))
    print(f"Solved {solved}/{total} (timeouts: {timeouts})")
    if failed:
        print("First failures:", ", ".join(map(str, failed[:10])))
    print()
    return {"solved": solved, "total": total, "timeouts": timeouts, "depth": depth}


# --------------------------------------------------------------------------- #
# 6. Lines of code
# --------------------------------------------------------------------------- #

def run_loc(src):
    section("6. CODE SIZE")
    files = []
    for ext in ("c", "h"):
        files += glob.glob(os.path.join(src, "**", f"*.{ext}"), recursive=True)
    if not files:
        print(f"No .c/.h files found under {src}\n")
        return None
    total = 0
    for fpath in files:
        with open(fpath, errors="ignore") as f:
            total += sum(1 for _ in f)
    print(f"{total:,} lines across {len(files)} files in {src}\n")
    return {"lines": total, "files": len(files)}


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #

def main():
    ap = argparse.ArgumentParser(description="Benchmark a UCI chess engine.")
    ap.add_argument("--engine", required=True, help="path to your compiled engine")
    ap.add_argument("--src", default="src", help="source dir for line count")
    ap.add_argument("--perft-depth", type=int, default=4, help="max perft depth (5 is slower)")
    ap.add_argument("--depths", type=int, nargs="+", default=[4, 5, 6, 7])
    ap.add_argument("--timeout", type=float, default=120, help="per-command timeout (s)")
    ap.add_argument("--stop-after", type=float, default=20,
                    help="stop increasing depth once a search takes longer than this (s)")
    ap.add_argument("--skip-perft", action="store_true")
    ap.add_argument("--skip-search", action="store_true")
    ap.add_argument("--compare", action="append", default=[], metavar="LABEL=PATH",
                    help="extra build to compare against --engine (repeatable)")
    ap.add_argument("--cmp-depth", type=int, default=6)
    ap.add_argument("--strength", action="store_true")
    ap.add_argument("--stockfish")
    ap.add_argument("--cutechess")
    ap.add_argument("--elos", type=int, nargs="+", default=[1400, 1600, 1800])
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--move-time", type=float, default=0.5, help="seconds per move for SF")
    ap.add_argument("--concurrency", type=int, default=1)
    ap.add_argument("--wac", help="EPD file (e.g. WAC.epd)")
    ap.add_argument("--wac-depth", type=int, default=6)
    ap.add_argument("--out", default="bench_results.json")
    args = ap.parse_args()

    if not os.path.isfile(args.engine):
        sys.exit(f"Engine not found: {args.engine}")

    results = {"engine": args.engine}
    eng = Engine(args.engine)
    try:
        if not args.skip_perft:
            results["perft"] = run_perft(eng, args.perft_depth, args.timeout)
        if not args.skip_search:
            results["search"] = run_search(eng, args.depths, args.timeout, args.stop_after)
        if args.compare:
            variants = []
            for item in args.compare:
                label, _, path = item.partition("=")
                if not path:
                    sys.exit(f"--compare expects LABEL=PATH, got: {item}")
                variants.append((label, path))
            results["compare"] = run_compare(args.engine, variants, args.cmp_depth, args.timeout)
        if args.wac:
            results["wac"] = run_wac(eng, args.wac, args.wac_depth, args.timeout)
    finally:
        eng.quit()

    if args.strength:
        results["strength"] = run_strength(args)
    results["loc"] = run_loc(args.src)

    with open(args.out, "w") as f:
        json.dump(results, f, indent=2)
    print(f"Saved {args.out}. Paste the tables above (or that file) back to get\n"
          "the README table and resume bullets filled in.")


if __name__ == "__main__":
    main()
