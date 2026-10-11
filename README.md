# Chess Engine in C

A UCI-compatible chess engine written in C. It represents the board with bitboards and searches with minimax, alpha-beta pruning and move-ordering heuristics.

## Features

- **Bitboard board representation** for compact state and fast move generation
- **Legal move generation** for all piece types
- **Minimax search with alpha-beta pruning** and iterative deepening
- **Static evaluation** combining material and piece-square tables
- **Move ordering** to maximize cutoffs:
  - MVV-LVA (Most Valuable Victim, Least Valuable Attacker) for captures
  - Promotion priority
  - Killer moves
  - History heuristic
- **UCI protocol support**, so it works with standard chess GUIs

## How It Works

### Board representation

Each piece type per side is stored as a 64-bit integer, one bit per square. Attack generation, occupancy checks and move generation reduce to bitwise operations (shifts, masks, AND/OR/XOR), which keeps the hot paths of the search fast and cache-friendly.

### Search

The engine runs minimax in negamax-style alpha-beta form. Alpha-beta prunes branches that cannot affect the final decision, and iterative deepening searches depth 1, 2, 3... so the best move from each shallower iteration seeds move ordering for the next one.

Good move ordering is what makes alpha-beta effective, so moves are scored and sorted before searching:

| Priority | Heuristic | Idea |
| --- | --- | --- |
| 1 | Captures (MVV-LVA) | Try "pawn takes queen" before "queen takes pawn" |
| 2 | Promotions | Promotions swing the evaluation the most |
| 3 | Killer moves | Quiet moves that caused a cutoff at the same depth in a sibling node |
| 4 | History heuristic | Quiet moves that have caused cutoffs often across the search |

### Evaluation

The static evaluation is the sum of material value and piece-square table bonuses for each side, so pieces are rewarded for standing on strong squares (centralized knights, advanced pawns, and so on).

## Getting Started

### Requirements

- A C compiler (GCC or Clang)
- `make`

### Build

```bash
git clone https://github.com/Dark74A/chess-engine.git
cd chess-engine
make
```

### Run

The engine speaks UCI over standard input and output. Start the binary and type commands directly:

```text
uci
isready
position startpos moves e2e4 e7e5
go depth 6
```

To play against it, add the compiled binary as a UCI engine in any UCI-compatible GUI (for example Arena, Cute Chess or Lucas Chess).

## Project Structure

```text
.
├── src/        # Engine source code
├── Makefile    # Build configuration
└── README.md
└── bench_tests.py   # Benchmarks: perft, search speed, strength tests
```

## Performance

| Metric | Result |
| --- | --- |
| Perft (start, Kiwipete, endgame; depth 1-4) | All counts match reference values |
| Move generation speed | 5-8M nodes/s |
| Search, start position | Depth 7 in 0.42 s (466,486 nodes, ~1.1M nodes/s) |
| Search, endgame position | Depth 7 in 0.19 s (165,163 nodes, ~0.9M nodes/s) |
| Search, Kiwipete (tactical middlegame) | Depth 6 in 22.4 s (9.2M nodes, ~0.4M nodes/s) |
| Hardware | [CPU, compiler flags] |

Run `python3 bench_tests.py --engine ./chess` to reproduce these numbers.

## Roadmap

- Transposition table with Zobrist hashing
- Quiescence search to reduce horizon-effect errors
- Null-move pruning and late move reductions
- Tapered (middlegame/endgame) evaluation
- Perft test suite to verify move generation

