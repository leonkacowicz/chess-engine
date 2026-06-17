#!/usr/bin/env python3
"""
Multi-move (cross-search) transposition-table benchmark.

The single-search benchmarks (one `go` from an empty TT) cannot see TT
*staleness*: entries left by earlier moves that go stale as the game advances
(pieces captured, pawns pushed, castling rights lost). This harness exercises
exactly that regime by driving the UCI engine through a *fixed* sequence of
positions with one persistent process (hence one persistent TT), searching each
to a fixed depth and summing nodes + time.

Comparing replacement policies is only fair if every policy searches the SAME
positions, so we:
  1. generate one game once via self-play and cache it to a file, then
  2. replay that fixed move list under whatever engine binary is given.

Run the SAME cached game against different engine builds (naive vs
depth-preferred vs aging/buckets) and compare total nodes / total time.

Usage:
  selfplay_bench.py --engine PATH [--depth D] [--plies N] [--game FILE]

The first run (no cached game) self-plays to create the game file; subsequent
runs replay it. Delete the game file to regenerate.
"""

import argparse
import os
import subprocess
import sys
import time


class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def uci(self):
        self.send("uci")
        for line in self.p.stdout:
            if line.strip() == "uciok":
                return

    def search(self, moves, depth):
        """Search startpos+moves to fixed depth. Returns (bestmove, nodes, time_ms)."""
        if moves:
            self.send("position startpos moves " + " ".join(moves))
        else:
            self.send("position startpos")
        self.send(f"go depth {depth}")
        nodes, time_ms, best = 0, 0, None
        for line in self.p.stdout:
            line = line.strip()
            if line.startswith("info") and " nodes " in line:
                tok = line.split()
                if "nodes" in tok:
                    nodes = int(tok[tok.index("nodes") + 1])
                if "time" in tok:
                    time_ms = int(tok[tok.index("time") + 1])
            elif line.startswith("bestmove"):
                best = line.split()[1]
                break
        return best, nodes, time_ms

    def quit(self):
        self.send("quit")
        try:
            self.p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.p.kill()


def generate_game(engine_path, depth, plies, game_file):
    print(f"generating {plies}-ply game at depth {depth} ...", flush=True)
    eng = Engine(engine_path)
    eng.uci()
    moves = []
    for i in range(plies):
        best, _, _ = eng.search(moves, depth)
        if best is None or best == "(none)":
            print(f"  game ended at ply {i}")
            break
        moves.append(best)
    eng.quit()
    with open(game_file, "w") as f:
        f.write(" ".join(moves) + "\n")
    print(f"  wrote {len(moves)} plies to {game_file}")
    return moves


def replay_bench(engine_path, depth, moves):
    eng = Engine(engine_path)
    eng.uci()
    total_nodes, total_time = 0, 0
    wall0 = time.time()
    for i in range(len(moves)):
        _, nodes, time_ms = eng.search(moves[:i], depth)
        total_nodes += nodes
        total_time += time_ms
    wall = time.time() - wall0
    eng.quit()
    return total_nodes, total_time, wall


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", required=True)
    ap.add_argument("--depth", type=int, default=8)
    ap.add_argument("--plies", type=int, default=60)
    ap.add_argument("--game", default=os.path.join(os.path.dirname(__file__), "game.txt"))
    args = ap.parse_args()

    if os.path.exists(args.game):
        with open(args.game) as f:
            moves = f.read().split()
        print(f"replaying cached game: {len(moves)} plies from {args.game}")
    else:
        moves = generate_game(args.engine, args.depth, args.plies, args.game)

    nodes, eng_ms, wall = replay_bench(args.engine, args.depth, moves)
    print(f"\n=== multi-move bench: {len(moves)} positions @ depth {args.depth} ===")
    print(f"total nodes (sum over positions): {nodes:,}")
    print(f"total engine search time:         {eng_ms/1000:.2f}s")
    print(f"wall clock:                       {wall:.2f}s")


if __name__ == "__main__":
    main()
