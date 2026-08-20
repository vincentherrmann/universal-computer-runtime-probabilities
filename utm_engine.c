#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// U(15,2) Fast Engine
// Symbols: c=0 (blank), b=1
// States: 1..15, Halt: 0

typedef struct {
    uint8_t next_state;
    uint8_t write_symbol;
    int8_t dir;
} Rule;

static Rule table[16][2];
static bool table_initialized = false;

static void init_table(void) {
    if (table_initialized) return;
    memset(table, 0, sizeof(table));

    // (state, sym): next_state, write_sym, dir (-1 = Left, +1 = Right)
    table[1][0]  = (Rule){ 2, 0,  1};
    table[1][1]  = (Rule){ 1, 1,  1};
    table[2][0]  = (Rule){ 3, 1,  1};
    table[2][1]  = (Rule){ 1, 1,  1};
    table[3][0]  = (Rule){ 7, 0, -1};
    table[3][1]  = (Rule){ 5, 0, -1};
    table[4][0]  = (Rule){ 6, 0, -1};
    table[4][1]  = (Rule){ 5, 1, -1};
    table[5][0]  = (Rule){ 1, 1,  1};
    table[5][1]  = (Rule){ 4, 1, -1};
    table[6][0]  = (Rule){ 4, 1, -1};
    table[6][1]  = (Rule){ 4, 1, -1};
    table[7][0]  = (Rule){ 8, 0, -1};
    table[7][1]  = (Rule){ 7, 1, -1};
    table[8][0]  = (Rule){ 9, 1, -1};
    table[8][1]  = (Rule){ 7, 1, -1};
    table[9][0]  = (Rule){ 1, 0,  1};
    table[9][1]  = (Rule){10, 1, -1};
    table[10][0] = (Rule){11, 1, -1};
    table[10][1] = (Rule){ 0, 1,  1}; // (10, b) is HALT (next_state = 0)
    table[11][0] = (Rule){12, 0,  1};
    table[11][1] = (Rule){14, 1,  1};
    table[12][0] = (Rule){13, 0,  1};
    table[12][1] = (Rule){12, 1,  1};
    table[13][0] = (Rule){ 2, 0, -1};
    table[13][1] = (Rule){12, 1,  1};
    table[14][0] = (Rule){ 3, 0, -1};
    table[14][1] = (Rule){15, 0,  1};
    table[15][0] = (Rule){14, 0,  1};
    table[15][1] = (Rule){14, 1,  1};

    table_initialized = true;
}

typedef struct {
    int max_steps;
    int tape_size;
    int tape_mid;
    uint8_t* tape;
} UTMEngine;

UTMEngine* create_utm_engine(int max_steps) {
    init_table();
    UTMEngine* eng = (UTMEngine*)malloc(sizeof(UTMEngine));
    eng->max_steps = max_steps;
    eng->tape_size = 2 * max_steps + 1024;
    eng->tape_mid = max_steps + 512;
    eng->tape = (uint8_t*)calloc(eng->tape_size, sizeof(uint8_t));
    return eng;
}

void free_utm_engine(UTMEngine* eng) {
    if (eng) {
        if (eng->tape) free(eng->tape);
        free(eng);
    }
}

// Run simulation with initial program placed to the LEFT of the head at x = -prog_len ... -1
// Head starts at x = 0 (tape[mid]) in state u1 reading blank (c=0).
// Returns step count when halted, or -1 if max_steps is reached.
int run_utm_left(UTMEngine* eng, const uint8_t* prog, int prog_len, int initial_state) {
    int max_steps = eng->max_steps;
    uint8_t* tape = eng->tape;
    int mid = eng->tape_mid;

    // Place program on tape at x in [-prog_len, -1]
    int prog_start = mid - prog_len;
    int prog_end = mid;
    if (prog_len > 0 && prog != NULL) {
        memcpy(&tape[prog_start], prog, prog_len);
    }

    int pos = mid; // Head starts at x = 0
    int state = (initial_state >= 1 && initial_state <= 15) ? initial_state : 1;
    int min_pos = pos < prog_start ? pos : prog_start;
    int max_pos = pos > prog_end ? pos : prog_end;

    for (int step = 1; step <= max_steps; step++) {
        uint8_t sym = tape[pos];
        Rule r = table[state][sym];

        if (r.next_state == 0) {
            // Halted cleanly
            memset(&tape[min_pos], 0, max_pos - min_pos + 1);
            return step;
        }

        tape[pos] = r.write_symbol;
        pos += r.dir;
        state = r.next_state;

        if (pos < min_pos) min_pos = pos;
        if (pos > max_pos) max_pos = pos;
    }

    // Did not halt within max_steps
    memset(&tape[min_pos], 0, max_pos - min_pos + 1);
    return -1;
}
