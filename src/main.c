#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include "bitboard.h"

static bool applyUciMove(Board *board, const char *text) {
    size_t len = strlen(text);
    if (len != 4 && len != 5) return false;
    for (int i = 0; i < 4; ++i) {
        char c = text[i];
        bool bad = (i % 2 == 0) ? (c < 'a' || c > 'h') : (c < '1' || c > '8');
        if (bad) return false;
    }    
    
    Move requested = { (text[1]-'1')*8 + text[0]-'a', (text[3]-'1')*8 + text[2]-'a', 0, 0 };
    
    if (len == 5) {
        switch (tolower((unsigned char)text[4])) {
            case 'q': requested.promotionPiece=QUEEN; break; case 'r': requested.promotionPiece=ROOK; break;
            case 'b': requested.promotionPiece=BISHOP; break; case 'n': requested.promotionPiece=KNIGHT; break;
            default: return false;
        }
    
    }
    
    Move legal[512]; uint64_t count=0;
    
    generateLegalMovesToArray(board, legal, &count, 512);
    
    for (uint64_t i=0; i<count; ++i) {
        if (legal[i].from == requested.from && legal[i].to == requested.to &&
            legal[i].promotionPiece == requested.promotionPiece) {
            Undo undo;
            return applyMove(board, legal[i], &undo);
        }
    }
    return false;
}

static bool setPosition(Board *board, char *arguments) {
    
    while (isspace((unsigned char)*arguments)) arguments++;
    
    char *cursor = arguments;
    char *wordEnd = cursor;
    
    while (*wordEnd && !isspace((unsigned char)*wordEnd)) wordEnd++;
    
    char *moves = strstr(cursor, "moves ");
    char *moveTokens = NULL;
    
    if (moves && (moves == cursor || !isspace((unsigned char)moves[-1]))) moves = NULL;
    
    if ((size_t)(wordEnd - cursor) == 8 && strncmp(cursor, "startpos", 8) == 0) {
        boardSetup(board);
        if (moves && moves > wordEnd) moveTokens = moves + 6;
        else if (*wordEnd && !isspace((unsigned char)*wordEnd)) return false;
    } else if ((size_t)(wordEnd - cursor) == 3 && strncmp(cursor, "fen", 3) == 0) {
        while (isspace((unsigned char)*wordEnd)) wordEnd++;
        char *fenMoves = strstr(wordEnd, " moves ");
        if (fenMoves) *fenMoves = '\0';
        if (!boardFromFen(board, wordEnd)) return false;
        if (fenMoves) moveTokens = fenMoves + 7;
    } else return false;

    while (moveTokens && *moveTokens) {
        while (isspace((unsigned char)*moveTokens)) moveTokens++;
        
        if (!*moveTokens) break;
        
        char *end = moveTokens;
        
        while (*end && !isspace((unsigned char)*end)) end++;
        
        size_t length = (size_t)(end - moveTokens);
        char moveText[8];
        
        if (length >= sizeof(moveText)) return false;
        
        memcpy(moveText, moveTokens, length);
        
        moveText[length] = '\0';
        if (!applyUciMove(board, moveText)) return false;
        if (!*end) break;
        moveTokens = end + 1;
    }
    return true;
}

int main(void) {
    
    initAttackTables();
    Board board;
    boardSetup(&board);
    
    char line[4096];
    
    while (fgets(line, sizeof(line), stdin)) {
    
        char *save = NULL;
        char original[4096];
        size_t lineLength = strcspn(line, "\r\n");
        
        memcpy(original, line, lineLength);
        
        original[lineLength] = '\0';
        
        char *command = strtok_r(line, " \t\r\n", &save);
        
        if (!command) continue;
        if (strcmp(command, "position") == 0) {
            char *args = original + strlen("position");
            while (isspace((unsigned char)*args)) args++;
            char *end = args + strlen(args);
            while (end > args && isspace((unsigned char)end[-1])) *--end = '\0';
            if (!setPosition(&board, args)) fprintf(stderr, "info string invalid position command\n");
            continue;
        }
        if (strcmp(command, "uci") == 0) {
            puts("id name C Bitboard Chess"); puts("id author Open source"); puts("uciok"); fflush(stdout);
        } else if (strcmp(command, "isready") == 0) {
            puts("readyok"); fflush(stdout);
        } else if (strcmp(command, "ucinewgame") == 0) {
            boardSetup(&board);
        } else if (strcmp(command, "go") == 0) {
            
            int depth=4;
            char *token;
            
            while ((token = strtok_r(NULL, " \t\r\n", &save))) {
                if (strcmp(token, "depth") == 0) {
                    
                    char *value = strtok_r(NULL, " \t\r\n", &save);
                    char *end = NULL;
                    long parsed = value ? strtol(value, &end, 10) : 0;
                    
                    if (!value || *end || parsed < 1) { depth=4; }
                    else depth = parsed > MAX_DEPTH - 1 ? MAX_DEPTH - 1 : (int)parsed;
                } else if (strcmp(token, "perft") == 0) {
                    char *value = strtok_r(NULL, " \t\r\n", &save);
                    char *end = NULL; long parsed = value ? strtol(value, &end, 10) : 0;
                    
                    if (!value || *end || parsed < 0 || parsed > 20) parsed = 1;
                    printf("nodes %llu\n", (unsigned long long)countMoves(&board, (int)parsed)); fflush(stdout);
                    depth = 0; break;
                }
            }
            if (depth) {
                Move best = findBestMove(&board, depth);
                
                if (best.from < 0 || best.from > 63 || best.to < 0 || best.to > 63) puts("bestmove 0000");
                else {
                    printf("bestmove %c%c%c%c", fileChar(best.from%8), rankChar(best.from/8), fileChar(best.to%8), rankChar(best.to/8));
                    if (best.promotionPiece) printf("%c", promotionChar(best.promotionPiece));
                    putchar('\n');
                }
                fflush(stdout);
            }
        } else if (strcmp(command, "quit") == 0) break;
    }
    return 0;
}
