# ♟️ Chess Engine in C

A chess engine built in C using bitboards.

## 🚀 Features

- Bitboard-based board representation
- Move generation
- Alpha-beta pruning
- Evaluation with piece-square tables
- Move ordering heuristics
- UCI-compatible interface

---

## 🧠 Engine Architecture

- Bitboards for fast board state handling
- Minimax search with Alpha-Beta pruning
- Evaluation using material + positional scoring
- Move ordering:
    - MVV-LVA
    - Promotion priority
    - Killer moves + History Heuristics

---

## 🛠️ Build Instructions

```bash
make
./engine
