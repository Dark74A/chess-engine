#include "bitboard.h"
#include <stdbool.h>
#include <string.h>

Move killerMoves[KILLERS_PER_DEPTH][MAX_DEPTH];
static int historyTable[64][64];

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
    return true;;
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

static inline int isCapture(const Board *b, const Move m) {

    int toSq = m.to;
    return (b->occupied & bit(toSq)) != 0;
}

// -------------------- Ordering Moves --------------------

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

    for (uint64_t i = 0; i+1 < count; i++) {
        for (uint64_t j = i + 1; j < count; j++) {
            if (moves[j].score > moves[i].score) {
                const Move tmp = moves[i];
                moves[i] = moves[j];
                moves[j] = tmp;
            }
        }
    }
}

// ----------------------------------------------------------
int quiescence(Board *b, int alpha, int beta) {
    int stand_pat = evaluate(b);

    if (stand_pat >= beta)
        return beta;

    if (stand_pat > alpha)
        alpha = stand_pat;

    Move moves[256];
    uint64_t count = 0;

    generateLegalMovesToArray(b, moves, &count, 256);

    for (uint64_t i = 0; i < count; i++) {
        if (!isCapture(b, moves[i]))
            continue;

        Undo u;
        applyMove(b, moves[i], &u);

        int score = -quiescence(b, -beta, -alpha);

        unmakeMove(b, &u);

        if (score >= beta)
            return score;

        if (score > alpha)
            alpha = score;

        if (score > stand_pat)
            stand_pat = score;
    }

    return stand_pat;
}
int evaluate(Board *b) {
    int mg = 0;   // middlegame score
    int eg = 0;   // endgame score
    U64 tmp;


    /* ================= MATERIAL + PST ================= */

    // White Pieces

    tmp = b->wp;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg += 100 + PST_PAWN[r][f];
        eg += 100 + PST_PAWN_END[r][f];
    }

    tmp = b->wn;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg += 300 + PST_KNIGHT[r][f];
        eg += 300 + PST_KNIGHT_END[r][f];
    }

    tmp = b->wb;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg += 300 + PST_BISHOP[r][f];
        eg += 300 + PST_BISHOP_END[r][f];
    }

    tmp = b->wr;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg += 500 + PST_ROOK[r][f];
        eg += 500 + PST_ROOK_END[r][f];
    }

    tmp = b->wq;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg += 900 + PST_QUEEN[r][f];
        eg += 900 + PST_QUEEN_END[r][f];
    }

    tmp = b->wk;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg += 20000 + PST_KING_MID[r][f];
        eg += 20000 + PST_KING_END[r][f];
    }


// Black pieces

    tmp = b->bp;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg -= 100 + PST_PAWN[r][f];
        eg -= 100 + PST_PAWN[r][f];
    }

    tmp = b->bn;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg -= 300 + PST_KNIGHT[r][f];
        eg -= 300 + PST_KNIGHT[r][f];
    }

    tmp = b->bb;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg -= 300 + PST_BISHOP[r][f];
        eg -= 300 + PST_BISHOP[r][f];
    }

    tmp = b->br;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg -= 500 + PST_ROOK[r][f];
        eg -= 500 + PST_ROOK[r][f];
    }

    tmp = b->bq;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg -= 900 + PST_QUEEN[r][f];
        eg -= 900 + PST_QUEEN[r][f];
    }

    tmp = b->bk;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        int r = sq >> 3, f = sq & 7;
        mg -= 20000 + PST_KING_MID[r][f];
        eg -= 20000 + PST_KING_END[r][f];
    }



    /* ================= PAWN STRUCTURE ================= */

    int dp = doubledPawns(b, WHITE) - doubledPawns(b, BLACK);
    int ip = isolatedPawns(b, WHITE) - isolatedPawns(b, BLACK);
    int pp = passedPawns(b, WHITE) - passedPawns(b, BLACK);

    mg -= DOUBLED_PAWN_BONUS * dp;
    eg -= DOUBLED_PAWN_BONUS * dp;        // less severe in endgame

    mg -= ISOLATED_PAWN_BONUS_MG * ip;
    eg -= ISOLATED_PAWN_BONUS_EG * ip;

    mg += PASSED_PAWN_BONUS_MG * pp;
    eg += PASSED_PAWN_BONUS_EG * pp;        // VERY strong in endgame


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
    int s = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    return b->mover == WHITE ? s : -s;

}
int search(Board *b, int depth) {
    memset(historyTable, 0, sizeof(historyTable));
    memset(killerMoves, 0, sizeof(killerMoves));

    return minimax(b, depth, -10000000, 10000000, 1);
}
int minimax(Board *board, int depth, int alpha, int beta, int ply) {
    if (depth == 0) {
        return quiescence(board, alpha, beta);
    }

    Move moves[512];
    uint64_t mcount = 0;

    generateLegalMovesToArray(board, moves, &mcount, 512);

    if (mcount == 0) {
        if (inCheck_bit(board, board->mover)) {
            return -100000 - depth;
        }
        return 0;
    }

    orderMoves(board, moves, mcount, depth);

    for (uint64_t i = 0; i < mcount; i++) {
        int isCapture = (getCapturedPiece(board, moves[i].to) != -1);

        Undo u;
        applyMove(board, moves[i], &u);

        int score = -minimax(board, depth - 1,
                             -beta, -alpha, ply + 1);

        unmakeMove(board, &u);

        if (score >= beta) {
            if (!isCapture) {
                historyTable[moves[i].from][moves[i].to] += depth * depth;

                killerMoves[1][ply] = killerMoves[0][ply];
                killerMoves[0][ply] = moves[i];
            }
            return beta;
        }

        if (score > alpha)
            alpha = score;
    }

    return alpha;
}
