#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

#include "bitboard.h"
Move findBestMove(Board* board, int depth) {
    Move legalMoves[512];
    uint64_t moveCount = 0;

    generateLegalMovesToArray(board, legalMoves, &moveCount, 512);

    if (moveCount == 0) {
        Move nullMove = {0};
        return nullMove;
    }

    Move bestMove = legalMoves[0];
    int bestScore = INT_MIN;

    for (uint64_t i = 0; i < moveCount; i++) {
        Undo u;
        Move currentMove = legalMoves[i];

        applyMove(board, currentMove, &u);
        int score = -search(board, depth-1);
        unmakeMove(board, &u);

        if (score > bestScore) {
            bestScore = score;
            bestMove = currentMove;
        }
    }

    return bestMove;
}


void applyUciMove(Board* board, const char* moveStr) {
    Move m;
    memset(&m, 0, sizeof(m));
    int fromFile = moveStr[0] - 'a';
    int fromRank = moveStr[1] - '1';
    int toFile   = moveStr[2] - 'a';
    int toRank   = moveStr[3] - '1';

    m.from = fromRank * 8 + fromFile;
    m.to   = toRank   * 8 + toFile;

    m.promotionPiece = 0;

    if ((int)strlen(moveStr) >= 5) {
        switch(moveStr[4]) {
            case 'q': case 'Q': m.promotionPiece = QUEEN; break;
            case 'r': case 'R': m.promotionPiece = ROOK;  break;
            case 'b': case 'B': m.promotionPiece = BISHOP;break;
            case 'n': case 'N': m.promotionPiece = KNIGHT;break;
        }
    }

    Undo u;
    applyMove(board, m, &u);
}


int main() {
    initAttackTables();
    Board board;
    boardSetup(&board);

    // printf("%d\n", countMoves(board, 5));

    char line[4096];

    while (fgets(line, sizeof(line), stdin)) {

        // Command: uci
        if (strncmp(line, "uci", 3) == 0) {
            printf("uciok\n");
            fflush(stdout);
        }
        // Command: isready
        else if (strncmp(line, "isready", 7) == 0) {
            printf("readyok\n");
            fflush(stdout);
        }
        // Command: ucinewgame
        else if (strncmp(line, "ucinewgame", 10) == 0) {
            boardSetup(&board);
        }
        // Command: position [startpos|fen] moves ...
        else if (strncmp(line, "position", 8) == 0) {
            char* ptr = line + 9;
            if (strncmp(ptr, "startpos", 8) == 0) {
                boardSetup(&board);
                ptr += 8;
            } else if (strncmp(ptr, "fen", 3) == 0) {
                ptr += 3;
            }

            char* moves = strstr(ptr, "moves");
            if (moves) {
                moves += 6;
                char movesCopy[2048];
                strncpy(movesCopy, moves, sizeof(movesCopy)-1);
                movesCopy[sizeof(movesCopy)-1] = '\0';
                char* move = strtok(movesCopy, " \n");
                while (move) {
                    applyUciMove(&board, move);
                    move = strtok(NULL, " \n");
                }
            }
        }
        // Command: go ...
        else if (strncmp(line, "go", 2) == 0) {

            if (strstr(line, "perft")) {
                int depth = 1;
                sscanf(line, "go perft %d", &depth);

                uint64_t nodes = countMoves(&board, depth);
                printf("nodes %llu\n", (unsigned long long)nodes);
                fflush(stdout);
                continue;
            }

            int searchDepth = 4;
            char* depthPtr = strstr(line, "depth");
            if (depthPtr) {
                sscanf(depthPtr, "depth %d", &searchDepth);
            }

            Move bestMove = findBestMove(&board, searchDepth);

            if (bestMove.from < 0 || bestMove.from > 63) {
                printf("bestmove 0000\n");
                fflush(stdout);
                continue;
            }


            printf("bestmove %c%c%c%c",
                fileChar(bestMove.from % 8), rankChar(bestMove.from / 8),
                fileChar(bestMove.to   % 8), rankChar(bestMove.to   / 8)
            );
            if (bestMove.promotionPiece != 0) {
                printf("%c", promotionChar(bestMove.promotionPiece));
            }
            printf("\n");
            fflush(stdout);
        }

        // Command: quit
        else if (strncmp(line, "quit", 4) == 0) {
            break;
        }
    }

    return 0;
}
