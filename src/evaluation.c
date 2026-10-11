#define _POSIX_C_SOURCE 200809L   
#include "bitboard.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

Move killerMoves[KILLERS_PER_DEPTH][MAX_DEPTH];
static int historyTable[64][64];

uint64_t nodesSearched = 0;


#ifdef PST_RANK1_FIRST
#define PST_W(t, r, f) t[(r)][(f)]
#define PST_B(t, r, f) t[7 - (r)][(f)]
#else
#define PST_W(t, r, f) t[7 - (r)][(f)]
#define PST_B(t, r, f) t[(r)][(f)]
#endif

int computePhase(const Board* board) {
    int phase = MAX_PHASE;

    phase -= __builtin_popcountll(board->wn) * KNIGHT_PHASE;
    phase -= __builtin_popcountll(board->bn) * KNIGHT_PHASE;

    phase -= __builtin_popcountll(board->wb) * BISHOP_PHASE;
    phase -= __builtin_popcountll(board->bb) * BISHOP_PHASE;

    phase -= __builtin_popcountll(board->wr) * ROOK_PHASE;
    phase -= __builtin_popcountll(board->br) * ROOK_PHASE;

    phase -= __builtin_popcountll(board->wq) * QUEEN_PHASE;
    phase -= __builtin_popcountll(board->bq) * QUEEN_PHASE;

    if (phase < 0) phase = 0;
    return phase;
}

static void pawn_files_from_bitboard(U64 pawns, int files[8]) {
    for (int i = 0; i < 8; ++i) files[i] = 0;
    while (pawns) {
        int sq = pop_lsb(&pawns);
        files[fileOf(sq)]++;
    }
}
int doubledPawns(const Board * board,const int color){
    U64 pawns = (color == WHITE) ? board->wp : board->bp;
    int files[8];
    pawn_files_from_bitboard(pawns, files);
    int dbl = 0;
    for (int f = 0; f < 8; ++f) {
        if (files[f] > 1) {
            dbl += files[f] - 1;
        }
    }
    return dbl;
}
int isolatedPawns(const Board * board,const int color){
    U64 pawns = (color == WHITE) ? board->wp : board->bp;
    int files[8];
    pawn_files_from_bitboard(pawns, files);

    int isolated = 0;
    U64 tmp = pawns;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int f = fileOf(sq);
        int left = (f > 0) ? files[f-1] : 0;
        int right = (f < 7) ? files[f+1] : 0;
        if (left == 0 && right == 0) isolated++;
    }
    return isolated;
}
bool isPassedPawn(const Board* board, const int row, const int col, const int color) {
    U64 enemyPawns = (color == WHITE) ? board->bp : board->wp;
    int dir = (color == WHITE) ? 1 : -1;
    int r = row + dir;
    for (; r >= 0 && r < 8; r += dir) {
        for (int df = -1; df <= 1; ++df) {
            int c = col + df;
            if (c < 0 || c > 7) continue;
            int sq = sq_index(r, c);
            if (enemyPawns & bit(sq)) return false;
        }
    }
    return true;
}
int passedPawns(const Board * board, const int color){
    U64 pawns = (color == WHITE) ? board->wp : board->bp;
    int count = 0;
    U64 tmp = pawns;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = rankOf(sq), f = fileOf(sq);
        if (isPassedPawn(board, r, f, color)) count++;
    }
    return count;
}


static inline int kingSafetyMG(const Board *b, const int color) {
    U64 king = (color == WHITE) ? b->wk : b->bk;
    if (!king) return -200; // mate situation

    int sq = __builtin_ctzll(king);
    int r = sq >> 3;
    int f = sq & 7;

    int score = 0;

    // Castling / back rank safety
    if ((color == WHITE && r <= 1) || (color == BLACK && r >= 6))
        score += 10;
    else
        score -= 5;

    // Pawn shield
    int dir = (color == WHITE) ? 1 : -1;
    int fr = r + dir;

    if (fr >= 0 && fr < 8) {
        U64 pawns = (color == WHITE) ? b->wp : b->bp;
        for (int df = -1; df <= 1; df++) {
            int ff = f + df;
            if (ff < 0 || ff > 7) continue;
            if (pawns & bit(fr * 8 + ff))
                score += 5;
        }
    }

    return score;
}

int knightMobility(const Board *b, const int color) {
    U64 knights = (color == WHITE) ? b->wn : b->bn;
    U64 own = (color == WHITE) ? b->whitePieces : b->blackPieces;
    int m = 0;

    while (knights) {
        int sq = pop_lsb(&knights);
        m += __builtin_popcountll(knightAttacks[sq] & ~own);
    }
    return m;
}
int bishopMobility(const Board *b, const int color) {
    U64 bishops = (color == WHITE) ? b->wb : b->bb;
    const U64 own = (color == WHITE) ? b->whitePieces : b->blackPieces;
    int m = 0;

    while (bishops) {
        const int sq = pop_lsb(&bishops);
        m += __builtin_popcountll(bishopAttacks[sq] & ~own);
    }
    return m;
}

bool inCheck_bit(const Board* board, const int color){
    U64 kings = (color == WHITE) ? board->wk : board->bk;
    if (!kings) return false;
    int kingSq = __builtin_ctzll(kings);
    int kr = rankOf(kingSq), kf = fileOf(kingSq);
    return isAttacked(*board, kr, kf, color);
}

static inline int isEnPassant(const Board *b, const Move m) {
    return ((pieceAt(b, m.from) & 7) == PAWN) && m.to == b->enPassantSquare &&
           fileOf(m.from) != fileOf(m.to);
}

static inline int isCapture(const Board *b, const Move m) {
    return (b->occupied & bit(m.to)) != 0 || isEnPassant(b, m);
}


int pieceValue(const int piece){
    switch (piece) {
        case PAWN:   return 100;
        case KNIGHT : return 300;
        case BISHOP: return 301;
        case ROOK:   return 500;
        case QUEEN:  return 900;
        case KING:   return 20000;
        default:     return 0;
    }
}
int getCapturedPiece(const Board *b, const int toSq) {
    U64 toBB = 1ULL << toSq;

    if (b->mover == WHITE) {
        if (b->bp & toBB) return 0;
        if (b->bn & toBB) return 1;
        if (b->bb & toBB) return 2;
        if (b->br & toBB) return 3;
        if (b->bq & toBB) return 4;
        if (b->bk & toBB) return 5;
    } else {
        if (b->wp & toBB) return 0;
        if (b->wn & toBB) return 1;
        if (b->wb & toBB) return 2;
        if (b->wr & toBB) return 3;
        if (b->wq & toBB) return 4;
        if (b->wk & toBB) return 5;
    }

    return -1;
}
int getAttackerPiece(const Board *b, const int fromSq) {
    U64 fromBB = 1ULL << fromSq;

    if (b->mover == WHITE) {
        if (b->wp & fromBB) return 0;
        if (b->wn & fromBB) return 1;
        if (b->wb & fromBB) return 2;
        if (b->wr & fromBB) return 3;
        if (b->wq & fromBB) return 4;
        if (b->wk & fromBB) return 5;
    } else {
        if (b->bp & fromBB) return 0;
        if (b->bn & fromBB) return 1;
        if (b->bb & fromBB) return 2;
        if (b->br & fromBB) return 3;
        if (b->bq & fromBB) return 4;
        if (b->bk & fromBB) return 5;
    }

    return -1;
}
bool sameMove(const Move *a, const Move *b) {
    return a->from == b->from &&
           a->to == b->to &&
           a->promotionPiece == b->promotionPiece;
}
int scoreMove(const Board *b, const Move *m, const int ply) {

    /* ================= PROMOTIONS ================= */

    if (m->promotionPiece) {
        return SCORE_PROMO + pieceValue(m->promotionPiece);
    }

    /* ================= CAPTURES ================= */

    int victim = getCapturedPiece(b, m->to);
    if (victim == -1 && isEnPassant(b, *m)) victim = 0;
    if (victim != -1) {
        int attacker = getAttackerPiece(b, m->from);
        if (attacker == -1)
            return SCORE_CAPTURE;

        return SCORE_CAPTURE + MVV_LVA[victim][attacker];
    }

    /* ================= KILLERS ================= */

    if (sameMove(m, &killerMoves[0][ply]))
        return SCORE_KILLER;

    if (sameMove(m, &killerMoves[1][ply]))
        return SCORE_KILLER - 1;

    /* ================= HISTORY ================= */

    return SCORE_HISTORY + historyTable[m->from][m->to];
}
static void orderMoves(const Board *b, Move *moves, const uint64_t count, const int depth) {
    for (uint64_t i = 0; i < count; i++) {
        moves[i].score = scoreMove(b, &moves[i], depth);
    }

    for (uint64_t i = 1; i < count; ++i) {
        Move key = moves[i];
        uint64_t j = i;
        while (j > 0 && moves[j - 1].score < key.score) { moves[j] = moves[j - 1]; --j; }
        moves[j] = key;
    }
}

int quiescence(Board *b, int alpha, int beta) {
    nodesSearched++;
    bool checked = inCheck_bit(b, b->mover);
    int stand_pat = evaluate(b);
    if (!checked && stand_pat >= beta)
        return beta;
    if (!checked && stand_pat > alpha)
        alpha = stand_pat;

    Move moves[512];
    uint64_t count = 0;

    generateLegalMovesToArray(b, moves, &count, 512);
    if (checked && count == 0) return -100000;
    if (!checked && count == 0) return 0;
    orderMoves(b, moves, count, 0);

    for (uint64_t i = 0; i < count; i++) {
        if (!checked && !isCapture(b, moves[i]) && !moves[i].promotionPiece)
            continue;

        Undo u;
        if (!applyMove(b, moves[i], &u)) continue;   

        int score = -quiescence(b, -beta, -alpha);

        unmakeMove(b, &u);

        if (score >= beta)
            return score;

        if (score > alpha)
            alpha = score;

        if (score > stand_pat)
            stand_pat = score;
    }
    return checked ? alpha : stand_pat;
}

/* ===================== EVALUATION ===================== */

#define ADD_W(bb, value, MG_T, EG_T)                                  \
    do {                                                              \
        U64 t_ = (bb);                                                \
        while (t_) {                                                  \
            int sq_ = pop_lsb(&t_);                                   \
            int r_ = sq_ >> 3, f_ = sq_ & 7;                          \
            mg += (value) + PST_W(MG_T, r_, f_);                      \
            eg += (value) + PST_W(EG_T, r_, f_);                      \
        }                                                             \
    } while (0)

#define ADD_B(bb, value, MG_T, EG_T)                                  \
    do {                                                              \
        U64 t_ = (bb);                                                \
        while (t_) {                                                  \
            int sq_ = pop_lsb(&t_);                                   \
            int r_ = sq_ >> 3, f_ = sq_ & 7;                          \
            mg -= (value) + PST_B(MG_T, r_, f_);                      \
            eg -= (value) + PST_B(EG_T, r_, f_);                      \
        }                                                             \
    } while (0)

int evaluate(Board *b) {
    int mg = 0;   // middlegame score 
    int eg = 0;   // endgame score

    /* ================= MATERIAL + PST ================= */

    ADD_W(b->wp, 100, PST_PAWN,   PST_PAWN_END);
    ADD_W(b->wn, 300, PST_KNIGHT, PST_KNIGHT_END);
    ADD_W(b->wb, 300, PST_BISHOP, PST_BISHOP_END);
    ADD_W(b->wr, 500, PST_ROOK,   PST_ROOK_END);
    ADD_W(b->wq, 900, PST_QUEEN,  PST_QUEEN_END);
    ADD_W(b->wk, 0,   PST_KING_MID, PST_KING_END);

    ADD_B(b->bp, 100, PST_PAWN,   PST_PAWN_END);
    ADD_B(b->bn, 300, PST_KNIGHT, PST_KNIGHT_END);
    ADD_B(b->bb, 300, PST_BISHOP, PST_BISHOP_END);
    ADD_B(b->br, 500, PST_ROOK,   PST_ROOK_END);
    ADD_B(b->bq, 900, PST_QUEEN,  PST_QUEEN_END);
    ADD_B(b->bk, 0,   PST_KING_MID, PST_KING_END);

    /* ================= PAWN STRUCTURE ================= */

    int dp = doubledPawns(b, WHITE) - doubledPawns(b, BLACK);
    int ip = isolatedPawns(b, WHITE) - isolatedPawns(b, BLACK);
    int pp = passedPawns(b, WHITE) - passedPawns(b, BLACK);

    mg -= DOUBLED_PAWN_BONUS * dp;
    eg -= DOUBLED_PAWN_BONUS * dp;

    mg -= ISOLATED_PAWN_BONUS_MG * ip;
    eg -= ISOLATED_PAWN_BONUS_EG * ip;

    mg += PASSED_PAWN_BONUS_MG * pp;
    eg += PASSED_PAWN_BONUS_EG * pp;

    /* ================= POSITIONAL ================= */

    mg += kingSafetyMG(b, WHITE);
    mg -= kingSafetyMG(b, BLACK);

    /* ================= MOBILITY ================= */

    mg += knightMobility(b, WHITE) * 2;
    mg += bishopMobility(b, WHITE) * 2;

    mg -= knightMobility(b, BLACK) * 2;
    mg -= bishopMobility(b, BLACK) * 2;

    eg += knightMobility(b, WHITE);
    eg -= knightMobility(b, BLACK);

    /* ================= PHASE ================= */

    int phase = computePhase(b);
    int s = (mg * (MAX_PHASE - phase) + eg * phase) / MAX_PHASE;

    return b->mover == WHITE ? s : -s;
}

/* ===================== SEARCH ===================== */

int search(Board *b, int depth) {
    memset(historyTable, 0, sizeof(historyTable));
    memset(killerMoves, 0, sizeof(killerMoves));
    nodesSearched = 0;

    return minimax(b, depth, -10000000, 10000000, 0);
}

static void updateHistory(int from, int to, int depth) {
    historyTable[from][to] += depth * depth;
    if (historyTable[from][to] > HISTORY_MAX) {        
        for (int f = 0; f < 64; ++f)
            for (int t = 0; t < 64; ++t)
                historyTable[f][t] >>= 1;
    }
}

int minimax(Board *board, int depth, int alpha, int beta, int ply) {
    if (depth == 0) {
        return quiescence(board, alpha, beta);   
    }
    nodesSearched++;

    Move moves[512];
    uint64_t mcount = 0;

    generateLegalMovesToArray(board, moves, &mcount, 512);

    if (mcount == 0) {
        if (inCheck_bit(board, board->mover)) {
            return -100000 - depth;
        }
        return 0;
    }

    if (ply >= MAX_DEPTH - 1) return evaluate(board);
    orderMoves(board, moves, mcount, ply);

    for (uint64_t i = 0; i < mcount; i++) {
        int capture = isCapture(board, moves[i]);

        Undo u;
        if (!applyMove(board, moves[i], &u)) continue;   

        int score = -minimax(board, depth - 1,
                             -beta, -alpha, ply + 1);

        unmakeMove(board, &u);

        if (score >= beta) {
            if (!capture && !moves[i].promotionPiece) {
                updateHistory(moves[i].from, moves[i].to, depth);

                if (!sameMove(&moves[i], &killerMoves[0][ply])) {
                    killerMoves[1][ply] = killerMoves[0][ply];
                    killerMoves[0][ply] = moves[i];
                }
            }
            return beta;
        }

        if (score > alpha)
            alpha = score;
    }

    return alpha;
}

static long long nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

Move findBestMove(Board *board, int maxDepth) {
    Move best = {-1, -1, 0, 0};
    Move moves[512]; uint64_t count = 0;
    generateLegalMovesToArray(board, moves, &count, 512);
    if (!count) return best;
    if (maxDepth < 1) maxDepth = 1;
    if (maxDepth >= MAX_DEPTH) maxDepth = MAX_DEPTH - 1;

    memset(historyTable, 0, sizeof(historyTable));
    memset(killerMoves, 0, sizeof(killerMoves));
    nodesSearched = 0;
    long long start = nowMs();

    orderMoves(board, moves, count, 0);
    best = moves[0];

    for (int d = 1; d <= maxDepth; ++d) {
        for (uint64_t i = 1; i < count; ++i) {
            if (sameMove(&moves[i], &best)) {
                Move tmp = moves[i];
                memmove(&moves[1], &moves[0], i * sizeof(Move));
                moves[0] = tmp;
                break;
            }
        }

        int alpha = -10000000, beta = 10000000;
        Move iterBest = moves[0];
        for (uint64_t i = 0; i < count; ++i) {
            Undo undo;
            if (!applyMove(board, moves[i], &undo)) continue;
            int score = -minimax(board, d - 1, -beta, -alpha, 1);
            unmakeMove(board, &undo);
            if (score > alpha) { alpha = score; iterBest = moves[i]; }
        }
        best = iterBest;

        long long ms = nowMs() - start;
        printf("info depth %d score cp %d nodes %llu time %lld nps %llu\n",
               d, alpha, (unsigned long long)nodesSearched, ms,
               ms > 0 ? (unsigned long long)(nodesSearched * 1000ULL / (unsigned long long)ms) : 0ULL);
        fflush(stdout);
    }
    return best;
}
