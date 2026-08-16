#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Added for printf

// --- Data Structures ---

// Represents one simulation state packed into bits
// For width=512, we need 8 x 64-bit integers.
typedef struct {
    uint64_t* words;
    int num_words;
    int width_bits;
} State;

// A simple hash table entry
typedef struct {
    int generation;     // Used to invalidate entries without memset
    int step_index;     // At which step was this seen?
    uint64_t* state_data; // Pointer to the stored state in the arena
} HashEntry;

// The Engine holds all memory to avoid re-allocation
typedef struct {
    int max_steps;
    int width;
    int num_words;

    // Arena for storing historical states to compare against
    // Layout: A giant block of uint64_t.
    // storage[step * num_words] is the start of the state for that step.
    uint64_t* history_arena;

    // Hash Table for O(1) loop detection
    HashEntry* hash_table;
    size_t hash_size;

    int current_generation;
} Engine;

// --- Helpers ---

// Mix bits for a quick hash
static inline uint64_t hash_state(const uint64_t* data, int num_words) {
    uint64_t h = 0xcbf29ce484222325ULL; // FNV offset
    for (int i = 0; i < num_words; i++) {
        h ^= data[i];
        h *= 0x109951162821199ULL; // FNV prime
    }
    return h;
}

// Bitwise implementation of Rule 110
// Formula: (~L & R) | (C ^ R)
// This calculates 64 cells in parallel.
static inline void evolve_state(uint64_t* current, uint64_t* next, int num_words) {
    for (int i = 0; i < num_words; i++) {
        uint64_t C = current[i];

        uint64_t prev_word = (i == 0) ? current[num_words - 1] : current[i - 1];
        uint64_t next_word = (i == num_words - 1) ? current[0] : current[i + 1];

        // CORRECTED: L shifts Right (>>). R shifts Left (<<).
        // L pulls the LSB (bit 0) of prev_word into the MSB (bit 63)
        // R pulls the MSB (bit 63) of next_word into the LSB (bit 0)
        uint64_t L = (C >> 1) | (prev_word << 63);
        uint64_t R = (C << 1) | (next_word >> 63);

        next[i] = (~L & R) | (C ^ R);
    }
}

// --- API ---

Engine* create_engine(int width, int max_steps) {
    Engine* eng = (Engine*)malloc(sizeof(Engine));
    eng->width = width;
    eng->num_words = width / 64;
    eng->max_steps = max_steps;
    eng->current_generation = 0;

    // Hash table size: Power of 2 > max_steps * 2 for low load factor
    eng->hash_size = 1;
    while (eng->hash_size <= (size_t)max_steps * 2) eng->hash_size <<= 1;

    eng->hash_table = (HashEntry*)calloc(eng->hash_size, sizeof(HashEntry));

    // Allocate ONE block of memory for all history states
    // size: (max_steps + 1) * num_words * 8 bytes
    eng->history_arena = (uint64_t*)malloc((max_steps + 1) * eng->num_words * sizeof(uint64_t));

    return eng;
}

void free_engine(Engine* eng) {
    if (eng) {
        free(eng->hash_table);
        free(eng->history_arena);
        free(eng);
    }
}

// Returns: step number where loop was closed, or -1 if no loop found.
// Writes the final state into result_buffer (must be uint8_t array of size width).
int run_simulation(Engine* eng, const uint8_t* initial_state, uint8_t* result_buffer, int verbose) {
    eng->current_generation++;
    int cur_gen = eng->current_generation;
    int num_words = eng->num_words;
    uint64_t mask = eng->hash_size - 1;

    // 1. Pack initial state into the first slot of history arena
    uint64_t* current_state_ptr = &eng->history_arena[0];
    memset(current_state_ptr, 0, num_words * sizeof(uint64_t));

    for (int i = 0; i < eng->width; i++) {
        if (initial_state[i]) {
            current_state_ptr[i / 64] |= (1ULL << (63 - (i % 64)));
        }
    }

    // 2. Loop
    for (int step = 0; step < eng->max_steps; step++) {

        // --- DEBUG PRINTING ---
        if (verbose) {
            printf("Step %4d: ", step);
            for (int i = 0; i < eng->width; i++) {
                int w_idx = i / 64;
                int b_idx = 63 - (i % 64);
                int bit = (current_state_ptr[w_idx] >> b_idx) & 1;
                // Use putc for speed, though printf is fine too
                putc(bit ? '1' : '.', stdout);
            }
            putc('\n', stdout);
        }
        // ----------------------

        // a. Hash current state
        uint64_t h = hash_state(current_state_ptr, num_words);
        size_t idx = h & mask;

        // b. Check Hash Map (Linear Probing)
        while (1) {
            HashEntry* entry = &eng->hash_table[idx];

            if (entry->generation != cur_gen) {
                entry->generation = cur_gen;
                entry->step_index = step;
                entry->state_data = current_state_ptr;
                break;
            }

            bool match = true;
            for(int w=0; w<num_words; w++) {
                if (current_state_ptr[w] != entry->state_data[w]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                if (verbose) printf("Loop detected! Returning to step %d\n", entry->step_index);

                // Unpack state to result buffer
                for (int i = 0; i < eng->width; i++) {
                    int w_idx = i / 64;
                    int b_idx = 63 - (i % 64);
                    result_buffer[i] = (current_state_ptr[w_idx] >> b_idx) & 1;
                }
                return step;
            }

            idx = (idx + 1) & mask;
        }

        // c. Evolve state
        uint64_t* next_state_ptr = &eng->history_arena[(step + 1) * num_words];
        evolve_state(current_state_ptr, next_state_ptr, num_words);

        current_state_ptr = next_state_ptr;
    }

    return -1;
}