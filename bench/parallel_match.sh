#!/bin/bash
# Parallel self-play match between two UCI engine binaries, for validating an
# eval/search change by STRENGTH (not node count). Plays a fixed opening set
# both colour-ways at a fixed movetime, many games concurrently, and reports the
# score from ENGINE_A's perspective.
#
# Usage:
#   parallel_match.sh ENGINE_A ENGINE_B [movetime_ms=100] [rounds=2] [jobs=16]
#
#   ENGINE_A, ENGINE_B : paths to two chessengine (UCI) binaries
#   movetime_ms        : per-move wall-clock budget. The match is TIME-controlled
#                        on purpose, so an engine that reaches more depth per ms
#                        is rewarded -- the right way to judge eval/search changes.
#   rounds             : repeats of the opening set (timing jitter -> fresh games)
#   jobs               : concurrent games. Each game is turn-based, so only ONE
#                        of its two engines searches at a time -- but the blocked
#                        opponents, arbiters and OS also use cores, so the safe
#                        rule is jobs <= ~cores/3, NOT cores. Measured on a 24-core
#                        box: jobs=4 matched a fair run, but jobs=16 biased ~3pp
#                        against the heavier engine (throttled wall-clock). When
#                        in doubt, lower jobs -- timing fidelity > throughput.
#
# Arbiter binary: $ARBITER, else the sibling chess-arbiter build.
set -u
A=${1:?usage: parallel_match.sh ENGINE_A ENGINE_B [movetime_ms=100] [rounds=2] [jobs=16]}
B=${2:?need ENGINE_B path}
MT=${3:-100}
ROUNDS=${4:-2}
JOBS=${5:-8}   # conservative for timing fidelity; raise only if cores are plentiful

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ARB=${ARBITER:-$HERE/../../chess-arbiter/build/src/arbiter/chessarbiter}
[ -x "$ARB" ] || { echo "arbiter not found/executable: $ARB (set \$ARBITER)" >&2; exit 1; }
[ -x "$A" ] && [ -x "$B" ] || { echo "engine binaries must be executable" >&2; exit 1; }
RES=$(mktemp -d)
trap 'rm -rf "$RES"' EXIT

openings=(
"e2e4 e7e5" "e2e4 c7c5" "e2e4 e7e6" "e2e4 c7c6" "e2e4 d7d6" "e2e4 g7g6"
"e2e4 g8f6" "e2e4 d7d5" "d2d4 d7d5" "d2d4 g8f6" "d2d4 f7f5" "d2d4 e7e6"
"d2d4 g7g6" "d2d4 c7c5" "c2c4 e7e5" "c2c4 c7c5" "c2c4 g8f6" "c2c4 e7e6"
"g1f3 d7d5" "b1c3 d7d5"
)

play_game() {   # id  side(A|B)  opening
  local id="$1" side="$2" op="$3" we be
  if [ "$side" = A ]; then we="$A"; be="$B"; else we="$B"; be="$A"; fi
  timeout 120 "$ARB" --white-exec "$we" --black-exec "$be" \
    --white-move-time "$MT" --black-move-time "$MT" \
    --initial-pos "startpos moves $op" --output "$RES/g$id.pgn" >/dev/null 2>&1
  echo "$side $(grep -oE '1-0|0-1|1/2-1/2' "$RES/g$id.pgn" 2>/dev/null | tail -1)" > "$RES/r$id"
}

id=0; running=0; start=$SECONDS
for ((r = 1; r <= ROUNDS; r++)); do
  for op in "${openings[@]}"; do
    for side in A B; do
      id=$((id + 1))
      play_game "$id" "$side" "$op" &
      running=$((running + 1))
      if [ "$running" -ge "$JOBS" ]; then wait -n; running=$((running - 1)); fi
    done
  done
done
wait
elapsed=$((SECONDS - start))

w=0; d=0; l=0; n=0   # from A's perspective
for rf in "$RES"/r*; do
  read -r side res < "$rf"; n=$((n + 1))
  if [ "$side" = A ]; then
    case "$res" in "1-0") w=$((w+1));; "0-1") l=$((l+1));; "1/2-1/2") d=$((d+1));; esac
  else
    case "$res" in "1-0") l=$((l+1));; "0-1") w=$((w+1));; "1/2-1/2") d=$((d+1));; esac
  fi
done
awk -v w=$w -v d=$d -v l=$l -v g=$n -v t=$elapsed -v mt=$MT -v j=$JOBS 'BEGIN{
  s = w + d/2;
  printf "A vs B @ %dms, jobs=%d, %d games in %ds\n", mt, j, g, t;
  printf "  A: +%d =%d -%d   score %.1f/%d = %.1f%%   (1 sigma ~%.1f%%)\n",
         w, d, l, s, g, 100*s/g, 100*sqrt(0.25/g) }'
