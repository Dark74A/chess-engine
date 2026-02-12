#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>



typedef uint64_t U64;

// Defining structs for board, Move, MoveList and Undo moves.
typedef struct {
    U64 wp, wn, wb, wr, wq, wk;
    U64 bp, bn, bb, br, bq, bk;

    U64 whitePieces;
    U64 blackPieces;
    U64 occupied;

    bool shortBlack;
    bool longBlack;
    bool shortWhite;
    bool longWhite;
    int enPassantSquare;

    int mover;
} Board;
typedef struct {
    int from;
    int to;
    int promotionPiece;
    int score;
} Move;
typedef struct {
    size_t size;
    Move* moves;
    size_t count;
} MoveList;
typedef struct {
    int from;
    int to;
    int movedPieceCode;
    int capturedPieceCode;
    int capturedSquare;
    bool prevShortBlack, prevLongBlack, prevShortWhite, prevLongWhite;
    int prevEnPassant;
    int prevMover;

    int rookFromSq;
    int rookToSq;
    int rookPieceCode;

    bool wasEnPassant;
} Undo;

// Enums
enum {
    WHITE = 0, BLACK = 8,
    PAWN = 1, KNIGHT = 2,  BISHOP = 3,  ROOK = 4,  QUEEN = 5,  KING = 6,
};

#define COLOR_MASK 0b1000

/*
 *  Helper
 *  pieceCode & 7 == piece type
 *  pieceCode & COLOR_MASK (8) == color
*/

// ----------------- small helpers -----------------

static inline U64 bit(int sq) {
    assert(sq >= 0 && sq < 64);
    return 1ULL << sq;
}

static inline int rankOf(int sq) {
    return sq >> 3;
}
static inline int fileOf(int sq) {
    return sq & 7;
}
static inline int sq_index(int rank, int file) {
    return rank * 8 + file;
}
// Removes least significant 1 bit
static inline int pop_lsb(U64 *b) {
    U64 bb = *b;
    int idx = __builtin_ctzll(bb); // returns least significant 1
    *b = bb & (bb - 1);
    return idx;
}

// ----------------- attack tables -----------------

U64 knightAttacks[64];
U64 kingAttacks[64];
U64 bishopAttacks[64];

const int knightOffsets[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
const int kingOffsets[8][2]   = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
const int bishopOffsets[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
const int rookOffsets[4][2]   = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};

bool insideFileRank(int r, int f) { 
    return r >= 0 && r < 8 && f >= 0 && f < 8;
}
U64 rayAttacksFrom(int sq, int dr, int df, U64 occupancy);
void initAttackTables() {
    for (int sq = 0; sq < 64; ++sq) {
        int r = rankOf(sq);
        int f = fileOf(sq);

        U64 kA = 0ULL;
        U64 nA = 0ULL;

        for (int i = 0; i < 8; ++i) {
            int rr = r + kingOffsets[i][0];
            int ff = f + kingOffsets[i][1];
            if (insideFileRank(rr, ff)) {
                kA |= bit(sq_index(rr, ff));
            }
        }

        for (int i = 0; i < 8; ++i) {
            int rr = r + knightOffsets[i][0];
            int ff = f + knightOffsets[i][1];
            if (insideFileRank(rr, ff)) {
                nA |= bit(sq_index(rr, ff));
            }
        }

        U64 bA = 0ULL;
        for (int i = 0; i < 4; ++i) {
            bA |= rayAttacksFrom(sq, bishopOffsets[i][0], bishopOffsets[i][1], 0ULL);
        }

        bishopAttacks[sq] = bA;
        kingAttacks[sq] = kA;
        knightAttacks[sq] = nA;

    }
}

// ----------------- Move List -----------------

void initMoveList(MoveList* mL, size_t size){
    mL->size = size;
    mL->moves = (Move *) malloc(sizeof(Move) * size);
    mL->count = 0;
}
void addMove(MoveList* mL, Move move){
    if (mL->size == mL->count){
        size_t newSize = mL->size * 2;
        mL->moves = (Move*) realloc(mL->moves, sizeof(Move) * newSize);
        mL->size = newSize;
    }

    mL->moves[mL->count++] = move;
}

// ----------------- Board -----------------

void updateOccupancies(Board* b) {
    b->whitePieces = b->wp | b->wn | b->wb | b->wr | b->wq | b->wk;
    b->blackPieces = b->bp | b->bn | b->bb | b->br | b->bq | b->bk;
    b->occupied = b->whitePieces | b->blackPieces;
}
int pieceAt(const Board* b, int sq) {
    U64 m = bit(sq);
    if (b->wp & m) return PAWN | WHITE;
    if (b->wn & m) return KNIGHT | WHITE;
    if (b->wb & m) return BISHOP | WHITE;
    if (b->wr & m) return ROOK | WHITE;
    if (b->wq & m) return QUEEN | WHITE;
    if (b->wk & m) return KING | WHITE;
    if (b->bp & m) return PAWN | BLACK;
    if (b->bn & m) return KNIGHT | BLACK;
    if (b->bb & m) return BISHOP | BLACK;
    if (b->br & m) return ROOK | BLACK;
    if (b->bq & m) return QUEEN | BLACK;
    if (b->bk & m) return KING | BLACK;
    return 0;
}
void clearSquare(Board* b, int sq) {
    U64 m = ~bit(sq);
    b->wp &= m; b->wn &= m; b->wb &= m; b->wr &= m; b->wq &= m; b->wk &= m;
    b->bp &= m; b->bn &= m; b->bb &= m; b->br &= m; b->bq &= m; b->bk &= m;
}
void setPiece(Board* b, int sq, int pieceCode) {
    clearSquare(b, sq);
    int piece = pieceCode & 7;
    int color = pieceCode & COLOR_MASK;
    U64 m = bit(sq);
    if (color == WHITE) {
        switch (piece) {
            case PAWN: b->wp |= m; break;
            case KNIGHT: b->wn |= m; break;
            case BISHOP: b->wb |= m; break;
            case ROOK: b->wr |= m; break;
            case QUEEN: b->wq |= m; break;
            case KING: b->wk |= m; break;
            default: ;
        }
    } else {
        switch (piece) {
            case PAWN: b->bp |= m; break;
            case KNIGHT: b->bn |= m; break;
            case BISHOP: b->bb |= m; break;
            case ROOK: b->br |= m; break;
            case QUEEN: b->bq |= m; break;
            case KING: b->bk |= m; break;
            default: ;
        }
    }
}
int findPieceCodeAt(const Board* b, int sq) {
    return pieceAt(b, sq);
}

// ----------------- Attacks & isAttacked -----------------

U64 rayAttacksFrom(int sq, int dr, int df, U64 occupancy) {
    U64 attacks = 0ULL;
    int r = rankOf(sq), f = fileOf(sq);
    while (1) {
        r += dr; f += df;
        if (!insideFileRank(r, f)) {
            break;
        }
        int t = sq_index(r, f);
        attacks |= bit(t);
        if (occupancy & bit(t)) {
            break;
        }
    }
    return attacks;
}
bool isAttacked(Board board, int row, int col, int color){
    int sq = sq_index(row, col);
    int attackerColor = (color == WHITE) ? BLACK : WHITE;

    // attacker bitboards
    // U64 atkP = (attackerColor == WHITE) ? board.wp : board.bp;
    U64 atkN = (attackerColor == WHITE) ? board.wn : board.bn;
    U64 atkB = (attackerColor == WHITE) ? board.wb : board.bb;
    U64 atkR = (attackerColor == WHITE) ? board.wr : board.br;
    U64 atkQ = (attackerColor == WHITE) ? board.wq : board.bq;
    U64 atkK = (attackerColor == WHITE) ? board.wk : board.bk;

    // Knights
    if (knightAttacks[sq] & atkN) {
        return true;
    }

    // King
    if (kingAttacks[sq] & atkK) {
        return true;
    }

    int r = row, f = col;
    if (attackerColor == WHITE) {
        int pr = r - 1;
        for (int df = -1; df <= 1; df += 2) {
            int pf = f + df;
            if (!insideFileRank(pr, pf)) {
                continue;
            }
            int psq = sq_index(pr, pf);
            if (board.wp & bit(psq)) {
                return true;
            }
        }
    } else {
        int pr = r + 1;
        for (int df = -1; df <= 1; df += 2) {
            int pf = f + df;
            if (!insideFileRank(pr, pf)) {
                continue;
            }
            int psq = sq_index(pr, pf);
            if (board.bp & bit(psq)) {
                return true;
            }
        }
    }

    // Sliding: rook/queen (orthogonal)
    U64 occ = board.occupied;
    for (int i = 0; i < 4; ++i) {
        int dr = rookOffsets[i][0];
        int df = rookOffsets[i][1];
        U64 rAtt = rayAttacksFrom(sq, dr, df, occ);
        if (rAtt & (atkR | atkQ)) {
            return true;
        }
    }

    // Sliding: bishop/queen (diagonal)
    for (int i = 0; i < 4; ++i) {
        int dr = bishopOffsets[i][0];
        int df = bishopOffsets[i][1];
        U64 bAtt = rayAttacksFrom(sq, dr, df, occ);
        if (bAtt & (atkB | atkQ)) {
            return true;
        }
    }

    return false;
}

// ----------------- Move generation -----------------

void addMoveToListFromTo(MoveList* mL, int fromSq, int toSq, int promo) {
    Move m;
    m.from = fromSq;
    m.to = toSq;
    m.promotionPiece = promo;
    addMove(mL, m);
}

void pawnMoves(Board* b, MoveList* mL, int sq) {
    int colorBit = ( (b->wp & bit(sq)) ? WHITE : (b->bp & bit(sq)) ? BLACK : -1 );
    if (colorBit == -1) {
        return;
    }

    int dir = (colorBit == WHITE) ? 1 : -1; 
    int startRank = (colorBit == WHITE) ? 1 : 6;
    int promotionRank = (colorBit == WHITE) ? 7 : 0;

    int r = rankOf(sq), f = fileOf(sq);
    int toR = r + dir;

    if (insideFileRank(toR, f) && !(b->occupied & bit(sq_index(toR, f)))) {
        int toSq = sq_index(toR, f);
        if (toR == promotionRank) {
            for (int p = 2; p <= 5; ++p) {
                addMoveToListFromTo(mL, sq, toSq, p);
            }
        } else {
            addMoveToListFromTo(mL, sq, toSq, 0);
        }

        int rr = r + 2*dir;
        if (r == startRank && insideFileRank(rr, f) && !(b->occupied & bit(sq_index(rr, f)))) {
            addMoveToListFromTo(mL, sq, sq_index(rr, f), 0);
        }
    }

    for (int df = -1; df <= 1; df += 2) {
        int tf = f + df;
        if (!insideFileRank(toR, tf)) {
            continue;
        }
        int toSq = sq_index(toR, tf);
        int targetCode = findPieceCodeAt(b, toSq);
        if (targetCode != 0) {
            int targetColor = targetCode & COLOR_MASK;
            if (targetColor != colorBit) {
                if (toR == promotionRank) {
                    for (int p = 2; p <= 5; ++p) addMoveToListFromTo(mL, sq, toSq, p);
                } else {
                    addMoveToListFromTo(mL, sq, toSq, 0);
                }
            }
        } else {
            if (b->enPassantSquare == toSq) {
                int capRank = toR - dir;
                int capSq = sq_index(capRank, tf);
                int capCode = findPieceCodeAt(b, capSq);
                if (capCode != 0 && (capCode & 7) == PAWN && (capCode & COLOR_MASK) != colorBit) {
                    addMoveToListFromTo(mL, sq, toSq, 0);
                }
            }
        }
    }
}
void knightMoves(Board* b, MoveList* mL, int sq) {
    int colorBit = ( (b->wn & bit(sq)) ? WHITE : (b->bn & bit(sq)) ? BLACK : -1 );
    if (colorBit == -1) {
        return;
    }
    U64 targets = knightAttacks[sq] & ~( (colorBit==WHITE) ? b->whitePieces : b->blackPieces );
    U64 t = targets;
    while (t) {
        int to = pop_lsb(&t);
        addMoveToListFromTo(mL, sq, to, 0);
    }
}
void slidingMoves(Board* b, MoveList* mL, int sq, const int directions[4][2], int dirCount) {
    int pieceCode = findPieceCodeAt(b, sq);
    if (pieceCode == 0) {
        return;
    }
    int colorBit = pieceCode & COLOR_MASK;
    for (int i = 0; i < dirCount; ++i) {
        int dr = directions[i][0];
        int df = directions[i][1];
        int r = rankOf(sq), f = fileOf(sq);
        while (1) {
            r += dr; f += df;
            if (!insideFileRank(r, f)) break;
            int to = sq_index(r, f);
            int targetCode = findPieceCodeAt(b, to);
            if (targetCode == 0) {
                addMoveToListFromTo(mL, sq, to, 0);
            } else {
                int tcolor = targetCode & COLOR_MASK;
                if (tcolor != colorBit) {
                    addMoveToListFromTo(mL, sq, to, 0);
                }
                break;
            }
        }
    }
}
void bishopMoves(Board* b, MoveList* mL, int sq) {
    slidingMoves(b, mL, sq, bishopOffsets, 4);
}
void rookMoves(Board* b, MoveList* mL, int sq) {
    slidingMoves(b, mL, sq, rookOffsets, 4);
}
void queenMoves(Board* b, MoveList* mL, int sq) {
    bishopMoves(b, mL, sq);
    rookMoves(b, mL, sq);
}
void kingMoves(Board* b, MoveList* mL, int sq) {
    int pieceCode = findPieceCodeAt(b, sq);
    if (pieceCode == 0) return;
    int colorBit = pieceCode & COLOR_MASK;
    U64 targets = kingAttacks[sq] & ~( (colorBit == WHITE) ? b->whitePieces : b->blackPieces );
    U64 t = targets;
    while (t) {
        int to = pop_lsb(&t);
        addMoveToListFromTo(mL, sq, to, 0);
    }

    // Castling (with attack checks)
    if (colorBit == WHITE && sq == sq_index(0,4)) {

        // King-side
        if (b->shortWhite &&
            !(b->occupied & bit(sq_index(0,5))) &&
            !(b->occupied & bit(sq_index(0,6))) &&
            !isAttacked(*b, 0, 4, WHITE) &&
            !isAttacked(*b, 0, 5, WHITE) &&
            !isAttacked(*b, 0, 6, WHITE))
        {
            addMoveToListFromTo(mL, sq, sq_index(0,6), 0);
        }

        // Queen-side
        if (b->longWhite &&
            !(b->occupied & bit(sq_index(0,3))) &&
            !(b->occupied & bit(sq_index(0,2))) &&
            !(b->occupied & bit(sq_index(0,1))) &&
            !isAttacked(*b, 0, 4, WHITE) &&
            !isAttacked(*b, 0, 3, WHITE) &&
            !isAttacked(*b, 0, 2, WHITE))
        {
            addMoveToListFromTo(mL, sq, sq_index(0,2), 0);
        }
    }

    if (colorBit == BLACK && sq == sq_index(7,4)) {

        if (b->shortBlack &&
            !(b->occupied & bit(sq_index(7,5))) &&
            !(b->occupied & bit(sq_index(7,6))) &&
            !isAttacked(*b, 7, 4, BLACK) &&
            !isAttacked(*b, 7, 5, BLACK) &&
            !isAttacked(*b, 7, 6, BLACK))
        {
            addMoveToListFromTo(mL, sq, sq_index(7,6), 0);
        }

        if (b->longBlack &&
            !(b->occupied & bit(sq_index(7,3))) &&
            !(b->occupied & bit(sq_index(7,2))) &&
            !(b->occupied & bit(sq_index(7,1))) &&
            !isAttacked(*b, 7, 4, BLACK) &&
            !isAttacked(*b, 7, 3, BLACK) &&
            !isAttacked(*b, 7, 2, BLACK))
        {
            addMoveToListFromTo(mL, sq, sq_index(7,2), 0);
        }
    }

}

// ----------------- Make / Unmake Move -----------------

int removePieceAt(Board* b, int sq) {
    assert(sq >= 0 && sq < 64);
    U64 m = bit(sq);
    if (b->wp & m) {
        b->wp &= ~m; return PAWN | WHITE;
    }
    if (b->wn & m) {
        b->wn &= ~m; return KNIGHT | WHITE;
    }
    if (b->wb & m) {
        b->wb &= ~m; return BISHOP | WHITE;
    }
    if (b->wr & m) {
        b->wr &= ~m; return ROOK | WHITE;
    }
    if (b->wq & m) {
        b->wq &= ~m; return QUEEN | WHITE;
    }
    if (b->wk & m) {
        b->wk &= ~m; return KING | WHITE;
    }
    if (b->bp & m) {
        b->bp &= ~m; return PAWN | BLACK;
    }
    if (b->bn & m) {
        b->bn &= ~m; return KNIGHT | BLACK;
    }
    if (b->bb & m) {
        b->bb &= ~m; return BISHOP | BLACK;
    }
    if (b->br & m) {
        b->br &= ~m; return ROOK | BLACK;
    }
    if (b->bq & m) {
        b->bq &= ~m; return QUEEN | BLACK;
    }
    if (b->bk & m) {
        b->bk &= ~m; return KING | BLACK;
    }
    return 0;
}
void placePieceAt(Board* b, int sq, int pieceCode) {
    assert(sq >= 0 && sq < 64);
    if (pieceCode == 0) {
        return;
    }
    setPiece(b, sq, pieceCode);
}
bool applyMove(Board* b, Move mv, Undo* u) {
    int fromSq   = mv.from;
    int toSq     = mv.to;
    int fromRank = fromSq / 8;
    int toRank   = toSq   / 8;
    int fromFile = fromSq % 8;
    int toFile   = toSq   % 8;

    /* ================= SAVE UNDO STATE ================= */

    u->from = fromSq;
    u->to   = toSq;

    u->movedPieceCode    = findPieceCodeAt(b, fromSq);
    u->capturedPieceCode = findPieceCodeAt(b, toSq);
    u->capturedSquare    = toSq;

    u->prevShortWhite = b->shortWhite;
    u->prevLongWhite  = b->longWhite;
    u->prevShortBlack = b->shortBlack;
    u->prevLongBlack  = b->longBlack;

    u->prevEnPassant = b->enPassantSquare;
    u->prevMover     = b->mover;

    u->wasEnPassant  = false;
    u->rookFromSq   = -1;
    u->rookToSq     = -1;
    u->rookPieceCode = 0;

    /* =================================================== */

    int movingCode = u->movedPieceCode;
    if (movingCode == 0) return false;

    int moverColor = movingCode & COLOR_MASK;
    int dir = (moverColor == WHITE) ? 1 : -1;

    /* ================= EN PASSANT CAPTURE ================= */

    if ((movingCode & 7) == PAWN &&
        fromFile != toFile &&
        findPieceCodeAt(b, toSq) == 0 &&
        b->enPassantSquare == toSq) {

        u->wasEnPassant = true;

        int capSq = toSq - dir * 8;
        u->capturedSquare    = capSq;
        u->capturedPieceCode = findPieceCodeAt(b, capSq);

        removePieceAt(b, capSq);
    }

    /* ================= NORMAL CAPTURE ================= */

    if (!u->wasEnPassant && u->capturedPieceCode != 0) {
        removePieceAt(b, toSq);
    }

    /* ================= MOVE PIECE ================= */

    removePieceAt(b, fromSq);

    if (mv.promotionPiece != 0) {
        placePieceAt(b, toSq, mv.promotionPiece | moverColor);
    } else {
        placePieceAt(b, toSq, movingCode);
    }

    /* ================= CASTLING ROOK MOVE ================= */

    if ((movingCode & 7) == KING &&
        abs(fromFile - toFile) == 2) {

        int kingRank = toRank;

        if (toFile == 6) {              // king-side
            u->rookFromSq = kingRank * 8 + 7;
            u->rookToSq   = kingRank * 8 + 5;
        } else {                        // queen-side
            u->rookFromSq = kingRank * 8 + 0;
            u->rookToSq   = kingRank * 8 + 3;
        }

        u->rookPieceCode = findPieceCodeAt(b, u->rookFromSq);

        int rookCode = removePieceAt(b, u->rookFromSq);
        if (rookCode)
            placePieceAt(b, u->rookToSq, rookCode);

        if (moverColor == WHITE) {
            b->shortWhite = b->longWhite = false;
        } else {
            b->shortBlack = b->longBlack = false;
        }
    }

    /* ================= UPDATE CASTLING RIGHTS ================= */

    if ((movingCode & 7) == KING) {
        if (moverColor == WHITE) {
            b->shortWhite = b->longWhite = false;
        } else {
            b->shortBlack = b->longBlack = false;
        }
    }

    if ((movingCode & 7) == ROOK) {
        if (fromSq == 0)  b->longWhite  = false;
        if (fromSq == 7)  b->shortWhite = false;
        if (fromSq == 56) b->longBlack  = false;
        if (fromSq == 63) b->shortBlack = false;
    }

    if ((u->capturedPieceCode & 7) == ROOK) {
        if (toSq == 0)  b->longWhite  = false;
        if (toSq == 7)  b->shortWhite = false;
        if (toSq == 56) b->longBlack  = false;
        if (toSq == 63) b->shortBlack = false;
    }

    /* ================= EN PASSANT SQUARE ================= */

    if ((movingCode & 7) == PAWN && abs(toRank - fromRank) == 2) {
        b->enPassantSquare = (fromRank + toRank) / 2 * 8 + fromFile;
    } else {
        b->enPassantSquare = -1;
    }

    /* ================= FINALIZE ================= */

    updateOccupancies(b);
    b->mover = (b->mover == WHITE) ? BLACK : WHITE;
    return true;
}
void unmakeMove(Board* b, Undo* u) {
    b->mover = u->prevMover;

    int fromSq = u->from;
    int toSq   = u->to;

    clearSquare(b, fromSq);
    clearSquare(b, toSq);

    placePieceAt(b, fromSq, u->movedPieceCode);

    if (u->capturedPieceCode != 0 && u->capturedSquare != -1) {
        placePieceAt(b, u->capturedSquare, u->capturedPieceCode);
    }

    if (u->rookFromSq != -1) {
        clearSquare(b, u->rookToSq);
        placePieceAt(b, u->rookFromSq, u->rookPieceCode);
    }

    b->shortBlack = u->prevShortBlack; b->longBlack = u->prevLongBlack;
    b->shortWhite = u->prevShortWhite; b->longWhite = u->prevLongWhite;
    b->enPassantSquare = u->prevEnPassant;

    updateOccupancies(b);
}

// ----------------- Move Generation Helpers -----------------

static inline void addMoveToArray(Move *moves, int *count, int maxMoves, Move m) {
    if (*count < maxMoves) {
        m.score = 0;
        moves[(*count)++] = m;
    }
}
void generateMoves(Board* b, MoveList* moveList) {
    updateOccupancies(b);

    U64 pawns, knights, bishops, rooks, queens, kings;

    if (b->mover == WHITE) {
        pawns   = b->wp;
        knights= b->wn;
        bishops= b->wb;
        rooks  = b->wr;
        queens = b->wq;
        kings  = b->wk;
    } else {
        pawns   = b->bp;
        knights= b->bn;
        bishops= b->bb;
        rooks  = b->br;
        queens = b->bq;
        kings  = b->bk;
    }


    // pawns
    U64 tmp = pawns;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        pawnMoves(b, moveList, sq);
    }

    // knights
    tmp = knights;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        knightMoves(b, moveList, sq);
    }

    // bishops
    tmp = bishops;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        bishopMoves(b, moveList, sq);
    }

    // rooks
    tmp = rooks;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        rookMoves(b, moveList, sq);
    }

    // queens
    tmp = queens;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        queenMoves(b, moveList, sq);
    }

    // kings
    tmp = kings;
    while (tmp) {
        int sq = pop_lsb(&tmp);
        kingMoves(b, moveList, sq);
    }

    // assert(moveList->count < moveList->size);

}
void generateMovesToArray(Board* b, Move* moves, uint64_t* outCount, int maxMoves) {
    MoveList m = {0};
    initMoveList(&m, 512);
    generateMoves(b, &m);

    int c = 0;
    for (size_t i = 0; i < m.count && c < maxMoves; ++i) {
        addMoveToArray(moves, &c, maxMoves, m.moves[i]);
    }
    *outCount = c;

    if (m.moves) {
        free(m.moves);
    }
}
void generateLegalMovesToArray(Board *board, Move *outMoves, uint64_t *outCount, size_t maxMoves) {
    Move temp[512];
    uint64_t tc = 0;
    generateMovesToArray(board, temp, &tc, 512);

    uint64_t idx = 0;
    for (uint64_t i = 0; i < tc; ++i) {
        Undo u;
        if (!applyMove(board, temp[i], &u)) {
            continue;
        }

        int moverColor = u.prevMover;

        int kingPos = -1;
        U64 kings = (moverColor == WHITE) ? board->wk : board->bk;
        if (kings) {
            kingPos = __builtin_ctzll(kings);
        }

        bool legal = true;
        if (kingPos == -1) {
            legal = false;
        } else {
            int kr = rankOf(kingPos), kf = fileOf(kingPos);
            if (isAttacked(*board, kr, kf, moverColor)) legal = false;
        }

        unmakeMove(board, &u);
        if (legal && idx < maxMoves) {
            outMoves[idx++] = temp[i];
        }

    }
    *outCount = idx;
}

// ----------------- Setup / Print / Utility -----------------

char pieceChar(int code) {
    int color = code & COLOR_MASK;
    int piece = code & 7;
    char c = '.';
    switch (piece) {
        case PAWN:   c = 'P'; break;
        case KNIGHT: c = 'N'; break;
        case BISHOP: c = 'B'; break;
        case ROOK:   c = 'R'; break;
        case QUEEN:  c = 'Q'; break;
        case KING:   c = 'K'; break;
        default:     c = '.'; break;
    }
    if (color == BLACK) c = (char)tolower((unsigned char)c);
    return c;
}
void printBoard(const Board *board) {
    printf("  a b c d e f g h\n");
    for (int i = 7; i >= 0; i--) {
        printf("%d ", i + 1);
        for (int j = 0; j < 8; j++) {
            const int code = findPieceCodeAt((Board*)board, sq_index(i,j));
            const char c = pieceChar(code);
            printf("%c ", c);
        }
        printf("%d\n", i + 1);
    }
    printf("  a b c d e f g h\n");
}
char fileChar(const int file) {
    return (char)('a' + file);
}
char rankChar(const int rank) {
    return (char)('1' + rank);
}
char promotionChar(const int promotion) {
    switch (promotion) {
        case KNIGHT: return 'N';
        case BISHOP: return 'B';
        case ROOK:   return 'R';
        case QUEEN:  return 'Q';
        default:     return '\0';
    }
}
void printMove(const Move m) {
    printf("%c%c%c%c",
        fileChar(m.from % 8), rankChar(m.from / 8),
        fileChar(m.to % 8), rankChar(m.to   / 8)
    );
    if (m.promotionPiece != 0) printf("%c", promotionChar(m.promotionPiece));
    printf("\n");
}
void printMoves(const MoveList* mL) {
    for (size_t i = 0; i < mL->count; i++) printMove(mL->moves[i]);
}

// ----------------- Starting position -----------------

void boardSetup(Board* b) {
    memset(b, 0, sizeof(Board));

    // white
    b->wr = bit(sq_index(0,0)) | bit(sq_index(0,7));
    b->wn = bit(sq_index(0,1)) | bit(sq_index(0,6));
    b->wb = bit(sq_index(0,2)) | bit(sq_index(0,5));
    b->wq = bit(sq_index(0,3));
    b->wk = bit(sq_index(0,4));
    b->wp = 0ULL;
    for (int f = 0; f < 8; ++f) {
        b->wp |= bit(sq_index(1,f));
    }

    // black
    b->br = bit(sq_index(7,0)) | bit(sq_index(7,7));
    b->bn = bit(sq_index(7,1)) | bit(sq_index(7,6));
    b->bb = bit(sq_index(7,2)) | bit(sq_index(7,5));
    b->bq = bit(sq_index(7,3));
    b->bk = bit(sq_index(7,4));
    b->bp = 0ULL;
    for (int f = 0; f < 8; ++f) {
        b->bp |= bit(sq_index(6,f));
    }

    b->shortBlack  = true; b->shortWhite = true; b->longBlack  = true; b->longWhite = true;
    b->enPassantSquare = -1;
    b->mover = WHITE;
    updateOccupancies(b);
}

// ----------------- Perft / counting -----------------

uint64_t countMoves(Board* board, int depth) {
    if (depth == 0) return 1;

    Move temp[512];
    size_t cnt = 0;

    generateLegalMovesToArray(board, temp, &cnt, 512);

    uint64_t count = 0;

    for (size_t i = 0; i < cnt; i++) {
        Undo u;
        applyMove(board, temp[i], &u);
        count += countMoves(board, depth - 1);
        unmakeMove(board, &u);
    }

    return count;
}
void printMoveShort(Move m) {
    printf("%c%c%c%c",
        fileChar(m.from % 8), rankChar(m.from / 8),
        fileChar(m.to % 8), rankChar(m.to / 8)
    );
    if (m.promotionPiece != 0) {
        printf("%c", promotionChar(m.promotionPiece));
    }
}
void perft_divide(Board board, int depth) {
    Move moves[512];
    uint64_t moveCount = 0;

    generateLegalMovesToArray(&board, moves, &moveCount, 512);

    uint64_t total = 0;

    for (uint64_t i = 0; i < moveCount; i++) {
        Undo u;
        applyMove(&board, moves[i], &u);

        uint64_t nodes = countMoves(&board, depth - 1);

        unmakeMove(&board, &u);

        total += nodes;
        printMoveShort(moves[i]);
        printf(" : %" PRIu64 "\n", nodes);
    }

    printf("divide depth %d total: %" PRIu64 "\n", depth, total);
}
int perft_main() {
    initAttackTables();
    Board board = {0};
    boardSetup(&board);

    printf("Enter: ");
    int depth = 0;
    if (scanf("%d", &depth) != 1) depth = 0;

    perft_divide(board, depth);

    return 0;
}

#ifdef TEST_MAIN
int main() {
    return perft_main();
}
#endif
