#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// 14-bit background ether pattern from Matthew Cook's universality proof:
// 11111000100110
// Temporal period is 7, spatial period is 14.
static const uint8_t ether_table[7][14] = {
    {1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0}, // t=0
    {1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1}, // t=1
    {1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0}, // t=2
    {1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1}, // t=3
    {1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1}, // t=4
    {0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0}, // t=5
    {0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0}  // t=6
};

static inline uint8_t get_ether(int x, int t) {
    int rem = x % 14;
    if (rem < 0) rem += 14;
    return ether_table[t % 7][rem];
}

// Engine for fast Rule 110 simulation on two-sided infinite tape with ether background
typedef struct {
    int max_steps;
    int width;       // Total width in bits (multiple of 64, >= 2 * max_steps + 128)
    int num_words;   // width / 64
    int w_left;      // Number of cells with negative positions: x in [-w_left, -1]
    int w_right;     // Number of cells with non-negative positions: x in [0, w_right - 1]

    uint64_t* state_a;
    uint64_t* state_b;
    uint64_t* mask;
    uint64_t* expected;
} Engine;

// --- API ---

// Create engine. If width < 2 * max_steps + 128, it is automatically sized to 2 * max_steps + 128
// to mathematically guarantee that speed-of-light perturbation never reaches outer boundaries.
Engine* create_engine(int width, int max_steps) {
    int required_width = 2 * max_steps + 128;
    if (width < required_width) {
        width = required_width;
    }
    if (width % 64 != 0) {
        width = ((width + 63) / 64) * 64;
    }
    Engine* eng = (Engine*)malloc(sizeof(Engine));
    eng->width = width;
    eng->num_words = width / 64;
    eng->max_steps = max_steps;
    eng->w_left = width / 2;
    eng->w_right = width - eng->w_left;

    eng->state_a = (uint64_t*)calloc(eng->num_words, sizeof(uint64_t));
    eng->state_b = (uint64_t*)calloc(eng->num_words, sizeof(uint64_t));
    eng->mask = (uint64_t*)calloc(eng->num_words, sizeof(uint64_t));
    eng->expected = (uint64_t*)calloc(eng->num_words, sizeof(uint64_t));

    return eng;
}

void free_engine(Engine* eng) {
    if (eng) {
        free(eng->state_a);
        free(eng->state_b);
        free(eng->mask);
        free(eng->expected);
        free(eng);
    }
}

// Run simulation with initial program placed at x = 0 ... prog_len - 1
// All other positions (x < 0 and x >= prog_len) are filled with the 14-bit ether pattern.
// Boundary conditions at left (x = -w_left) and right (x = w_right - 1) use dynamic ether E(x, t).
//
// Halting Criterion:
// Halts when the interval [-7, prog_len + 6] is identical to the original (t=0) ether pattern.
// Returns the step number when halted, or -1 if max_steps is reached without halting.
int run_simulation(
    Engine* eng,
    const uint8_t* program,
    int prog_len,
    uint8_t* result_buffer,
    int verbose
) {
    int width = eng->width;
    int num_words = eng->num_words;
    int w_left = eng->w_left;
    int max_steps = eng->max_steps;

    uint64_t* cur = eng->state_a;
    uint64_t* nxt = eng->state_b;
    uint64_t* mask = eng->mask;
    uint64_t* expected = eng->expected;

    // 1. Determine halting check boundaries
    int first_bit = w_left - 7;
    int last_bit = w_left + prog_len + 6;
    if (first_bit < 0) first_bit = 0;
    if (last_bit >= width) last_bit = width - 1;

    int first_word = first_bit / 64;
    int last_word = last_bit / 64;

    // Initial active words around the program and halting interval
    int cur_w_start = (w_left - 64) / 64;
    int cur_w_end = (w_left + prog_len + 64) / 64;
    if (cur_w_start < 0) cur_w_start = 0;
    if (cur_w_end >= num_words) cur_w_end = num_words - 1;

    // Clear active portion of buffers
    for (int w = cur_w_start; w <= cur_w_end; w++) {
        cur[w] = 0;
        mask[w] = 0;
        expected[w] = 0;
    }

    // 2. Initialize active tape with ether at t=0, overwritten by program at x = 0 .. prog_len - 1
    for (int w = cur_w_start; w <= cur_w_end; w++) {
        uint64_t word_val = 0;
        for (int b = 0; b < 64; b++) {
            int i = w * 64 + b;
            int x = i - w_left;
            uint8_t bit;
            if (x >= 0 && x < prog_len) {
                bit = program[x] ? 1 : 0;
            } else {
                bit = get_ether(x, 0);
            }
            if (bit) {
                word_val |= (1ULL << (63 - b));
            }
        }
        cur[w] = word_val;
    }

    // 3. Precompute halting masks for interval x in [-7, prog_len + 6]
    for (int i = first_bit; i <= last_bit; i++) {
        int w = i / 64;
        int b = 63 - (i % 64);
        int x = i - w_left;
        mask[w] |= (1ULL << b);
        if (get_ether(x, 0)) {
            expected[w] |= (1ULL << b);
        }
    }

    if (verbose) {
        printf("Step %4d: ", 0);
        int print_start = w_left - 20;
        int print_end = w_left + prog_len + 20;
        if (print_start < 0) print_start = 0;
        if (print_end > width) print_end = width;

        for (int i = print_start; i < print_end; i++) {
            int bit = (cur[i / 64] >> (63 - (i % 64))) & 1;
            putchar(bit ? '#' : '.');
        }
        putchar('\n');
    }

    // 4. Main simulation loop with adaptive light-cone expansion
    for (int step = 1; step <= max_steps; step++) {
        // Expand active words as light-cone spreads
        int req_w_start = (w_left - step - 1) / 64;
        int req_w_end = (w_left + prog_len + step) / 64;
        if (req_w_start < 0) req_w_start = 0;
        if (req_w_end >= num_words) req_w_end = num_words - 1;

        // Initialize newly entered words with pure background ether at step - 1
        while (cur_w_start > req_w_start) {
            cur_w_start--;
            uint64_t word_val = 0;
            for (int b = 0; b < 64; b++) {
                int pos = cur_w_start * 64 + b - w_left;
                if (get_ether(pos, step - 1)) {
                    word_val |= (1ULL << (63 - b));
                }
            }
            cur[cur_w_start] = word_val;
            mask[cur_w_start] = 0;
            expected[cur_w_start] = 0;
        }
        while (cur_w_end < req_w_end) {
            cur_w_end++;
            uint64_t word_val = 0;
            for (int b = 0; b < 64; b++) {
                int pos = cur_w_end * 64 + b - w_left;
                if (get_ether(pos, step - 1)) {
                    word_val |= (1ULL << (63 - b));
                }
            }
            cur[cur_w_end] = word_val;
            mask[cur_w_end] = 0;
            expected[cur_w_end] = 0;
        }

        // Boundary bits for active window
        uint64_t left_bit = (uint64_t)get_ether(cur_w_start * 64 - w_left - 1, step - 1);
        uint64_t right_bit = (uint64_t)get_ether((cur_w_end + 1) * 64 - w_left, step - 1);

        for (int i = cur_w_start; i <= cur_w_end; i++) {
            uint64_t C = cur[i];
            uint64_t prev_w = (i == cur_w_start) ? left_bit : cur[i - 1];
            uint64_t next_w = (i == cur_w_end) ? (right_bit << 63) : cur[i + 1];

            uint64_t L = (C >> 1) | (prev_w << 63);
            uint64_t R = (C << 1) | (next_w >> 63);

            nxt[i] = (~L & R) | (C ^ R);
        }

        uint64_t* tmp = cur;
        cur = nxt;
        nxt = tmp;

        if (verbose) {
            printf("Step %4d: ", step);
            int print_start = w_left - 20;
            int print_end = w_left + prog_len + 20;
            if (print_start < 0) print_start = 0;
            if (print_end > width) print_end = width;

            for (int i = print_start; i < print_end; i++) {
                int bit = (cur[i / 64] >> (63 - (i % 64))) & 1;
                putchar(bit ? '#' : '.');
            }
            putchar('\n');
        }

        // 5. Check halting condition: interval [-7, prog_len + 6] matches original ether
        bool match = true;
        for (int w = first_word; w <= last_word; w++) {
            if ((cur[w] & mask[w]) != expected[w]) {
                match = false;
                break;
            }
        }

        if (match) {
            if (verbose) {
                printf("--> HALTED at step %d (interval [-7, %d] returned to original ether pattern)\n", step, prog_len + 6);
            }

            if (result_buffer) {
                for (int i = 0; i < width; i++) {
                    result_buffer[i] = (cur[i / 64] >> (63 - (i % 64))) & 1;
                }
            }
            return step;
        }
    }

    // Did not halt within max_steps
    if (result_buffer) {
        for (int i = 0; i < width; i++) {
            result_buffer[i] = (cur[i / 64] >> (63 - (i % 64))) & 1;
        }
    }

    return -1;
}