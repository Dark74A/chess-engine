#ifndef BITBOARD_H
#define BITBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#define PAWN_PHASE    0
#define KNIGHT_PHASE  1
#define BISHOP_PHASE  1
#define ROOK_PHASE    2
#define QUEEN_PHASE   4

#define MAX_PHASE (PAWN_PHASE*16 + KNIGHT_PHASE*4 + BISHOP_PHASE*4 + ROOK_PHASE*4 + QUEEN_PHASE*2)
// = 24

#define DOUBLED_PAWN_BONUS (-10)
#define ISOLATED_PAWN_BONUS_MG (-10)
#define ISOLATED_PAWN_BONUS_EG (-20)
#define PASSED_PAWN_BONUS_MG 10
#define PASSED_PAWN_BONUS_EG 30

#define MAX_DEPTH 64
#define KILLERS_PER_DEPTH 2
#define SCORE_PROMO     9000000
#define SCORE_CAPTURE   8000000
#define SCORE_KILLER    7000000
#define SCORE_HISTORY   0


#define HISTORY_MAX 64

// #define ASSERT_SQ(sq) assert((sq) >= 0 && (sq) < 64)


#define COLOR_MASK 0b1000

#ifdef __cplusplus
extern "C" {
#endif


enum {
    WHITE = 0, BLACK = 8,
    PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6,
};


typedef uint64_t U64;
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

/* attack tables */
extern U64 knightAttacks[64];
extern U64 kingAttacks[64];
extern U64 bishopAttacks[64];

/* piece-square tables */
extern int PST_PAWN[8][8];
extern int PST_PAWN_END[8][8];
extern int PST_KNIGHT[8][8];
extern int PST_KNIGHT_END[8][8];
extern int PST_BISHOP[8][8];
extern int PST_BISHOP_END[8][8];
extern int PST_ROOK[8][8];
extern int PST_ROOK_END[8][8];
extern int PST_QUEEN[8][8];
extern int PST_QUEEN_END[8][8];
extern int PST_KING_MID[8][8];
extern int PST_KING_END[8][8];

/*  MVV-LVA Table  */

extern int MVV_LVA[6][6];

/* bit helpers */
static inline U64 bit(int sq) { return 1ULL << (sq); }
static inline int rankOf(int sq) { return (sq) >> 3; }
static inline int fileOf(int sq) { return (sq) & 7; }
static inline int sq_index(int rank, int file) { return (rank) * 8 + (file); }

/* pop lsb */
static inline int pop_lsb(U64 *b) {
    U64 bb = *b;
    int idx = __builtin_ctzll(bb);
    *b = bb & (bb - 1);
    return idx;
}

/* initialization / attack table */
void initAttackTables(void);

/* move list helpers */
void initMoveList(MoveList* mL, int size);
void addMove(MoveList* mL, Move move);

/* board helpers */
void updateOccupancies(Board* b);
int pieceAt(const Board* b, int sq);
void clearSquare(Board* b, int sq);
void setPiece(Board* b, int sq, int pieceCode);
int findPieceCodeAt(const Board* b, int sq);

/* attack queries */
U64 rayAttacksFrom(int sq, int dr, int df, U64 occupancy);
bool isAttacked(Board board, int row, int col, int color);

/* move generation */
void addMoveToListFromTo(MoveList* mL, int fromSq, int toSq, int promo);
void pawnMoves(Board* b, MoveList* mL, int sq);
void knightMoves(Board* b, MoveList* mL, int sq);
void slidingMoves(Board* b, MoveList* mL, int sq, const int directions[4][2], int dirCount);
void bishopMoves(Board* b, MoveList* mL, int sq);
void rookMoves(Board* b, MoveList* mL, int sq);
void queenMoves(Board* b, MoveList* mL, int sq);
void kingMoves(Board* b, MoveList* mL, int sq);
void generateMoves(Board* b, MoveList* moveList);

/* apply / make / unmake */
int removePieceAt(Board* b, int sq);
void placePieceAt(Board* b, int sq, int pieceCode);
void applyMove(Board* b, Move mv, Undo* u);
void unmakeMove(Board* b, Undo* u);

/* array-based move generation */
void generateMovesToArray(Board* b, Move* moves, int* outCount, int maxMoves);
void generateLegalMovesToArray(Board *board, Move *outMoves, uint64_t *outCount, int maxMoves);
void generateLegalMoves (Board * board, MoveList* moves);

/* helpers / printing */
char pieceChar(int code);
void printBoard(const Board *board);
char fileChar(int file);
char rankChar(int rank);
char promotionChar(int promotion);
void printMove(Move m);
void printMoves(const MoveList* mL);
void boardSetup(Board* b);

/* perft */
uint64_t countMoves(Board* board, int depth);
void perft_divide(Board board, int depth);
int perft_main(void);

int evaluate(Board* board);
int search(Board *b, int depth);
int minimax(Board * board, int depth, int alpha, int beta, int ply);


#ifdef __cplusplus
}
#endif

#endif /* BITBOARD_H */
