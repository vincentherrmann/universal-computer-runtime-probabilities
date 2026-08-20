#include "simulate_turing_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_PADDING 1024

/* --- Helper: Check if value is in int list --- */
int is_in_int_list(int value, int list_size, const int list[]) {
    if (list == NULL || list_size <= 0) return 0;
    for (int i = 0; i < list_size; i++) {
        if (list[i] == value) return 1;
    }
    return 0;
}

/* --- Tape Allocation & State Management --- */

struct turing_machine_state* create_initial_state(
    int initial_control_state,
    const char* input_string,
    long long initial_head_pos,
    symbol blank_symbol
) {
    struct turing_machine_state* state = (struct turing_machine_state*)malloc(sizeof(struct turing_machine_state));
    if (!state) {
        fprintf(stderr, "Error: Out of memory allocating Turing machine state.\n");
        exit(EXIT_FAILURE);
    }

    size_t input_len = input_string ? strlen(input_string) : 0;
    size_t cap = input_len + 2 * INITIAL_PADDING;
    if (cap < 2048) cap = 2048;

    state->control_state = initial_control_state;
    state->head_position = initial_head_pos;
    state->capacity = cap;
    state->tape = (symbol*)malloc(cap * sizeof(symbol));
    if (!state->tape) {
        fprintf(stderr, "Error: Out of memory allocating tape buffer.\n");
        free(state);
        exit(EXIT_FAILURE);
    }

    /* Initialize entire buffer with blank_symbol */
    memset(state->tape, blank_symbol, cap);

    /* Place input_string starting at offset = INITIAL_PADDING */
    state->offset = INITIAL_PADDING;
    state->min_index = -INITIAL_PADDING;
    state->max_index = (long long)cap - INITIAL_PADDING - 1;

    if (input_len > 0) {
        memcpy(&state->tape[state->offset], input_string, input_len);
    }

    return state;
}

void free_state(struct turing_machine_state* state) {
    if (state) {
        if (state->tape) free(state->tape);
        free(state);
    }
}

/* Ensure coordinate pos is addressable within state->tape, expanding if necessary */
static void ensure_tape_range(struct turing_machine_state* state, long long pos, symbol blank_symbol) {
    if (pos >= state->min_index && pos <= state->max_index) {
        return;
    }

    size_t old_cap = state->capacity;
    size_t new_cap = old_cap * 2;
    if (new_cap < old_cap + 4096) new_cap = old_cap + 4096;

    symbol* new_tape = (symbol*)malloc(new_cap * sizeof(symbol));
    if (!new_tape) {
        fprintf(stderr, "Error: Out of memory expanding tape.\n");
        exit(EXIT_FAILURE);
    }
    memset(new_tape, blank_symbol, new_cap);

    /* Center old buffer in the new buffer */
    long long additional_left = (new_cap - old_cap) / 2;
    long long new_offset = state->offset + additional_left;

    memcpy(&new_tape[additional_left], state->tape, old_cap);
    free(state->tape);

    state->tape = new_tape;
    state->capacity = new_cap;
    state->offset = new_offset;
    state->min_index = -new_offset;
    state->max_index = (long long)new_cap - new_offset - 1;
}

void update_state(
    struct turing_machine_state* state,
    int new_control_state,
    enum direction dir,
    symbol write_symbol,
    symbol blank_symbol
) {
    ensure_tape_range(state, state->head_position, blank_symbol);
    long long tape_idx = state->offset + state->head_position;
    state->tape[tape_idx] = write_symbol;

    state->control_state = new_control_state;
    state->head_position += (dir == DIR_LEFT ? -1 : 1);

    ensure_tape_range(state, state->head_position, blank_symbol);
}

void trace_state(const struct turing_machine_state* state, symbol blank_symbol) {
    long long head = state->head_position;
    int half_window = TRACE_TAPE_CHARS / 2;
    long long start = head - half_window;
    long long end = head + half_window;

    /* Print pointer line */
    printf("State u%-2d  pos=%-6lld [ ", state->control_state, head);
    for (long long p = start; p <= end; p++) {
        if (p == head) {
            putchar('v');
        } else {
            putchar(' ');
        }
    }
    printf(" ]\n");

    /* Print tape line */
    printf("Tape contents:            [ ");
    for (long long p = start; p <= end; p++) {
        if (p >= state->min_index && p <= state->max_index) {
            long long idx = state->offset + p;
            putchar(state->tape[idx]);
        } else {
            putchar(blank_symbol);
        }
    }
    printf(" ]\n");
}

/* --- Turing Machine Factory & Execution --- */

struct turing_machine create_turing_machine(int num_states, int initial_state, symbol blank) {
    struct turing_machine m;
    m.num_states = num_states;
    m.initial_control_state = initial_state;
    m.blank_symbol = blank;
    m.num_accepting_states = 0;
    m.accepting_states = NULL;

    /* Allocate (num_states + 1) rows so 1-based indexing state 1..num_states works directly */
    m.transition_table = (struct transition_result**)malloc((num_states + 1) * sizeof(struct transition_result*));
    if (!m.transition_table) {
        fprintf(stderr, "Error: Out of memory allocating transition table.\n");
        exit(EXIT_FAILURE);
    }

    for (int s = 0; s <= num_states; s++) {
        m.transition_table[s] = (struct transition_result*)malloc(TAPE_ALPHABET_SIZE * sizeof(struct transition_result));
        if (!m.transition_table[s]) {
            fprintf(stderr, "Error: Out of memory allocating transition table row.\n");
            exit(EXIT_FAILURE);
        }
        for (int a = 0; a < TAPE_ALPHABET_SIZE; a++) {
            m.transition_table[s][a].control_state = STATE_INVALID;
            m.transition_table[s][a].write_symbol = blank;
            m.transition_table[s][a].dir = DIR_RIGHT;
        }
    }

    return m;
}

void free_turing_machine(struct turing_machine* machine) {
    if (machine && machine->transition_table) {
        for (int s = 0; s <= machine->num_states; s++) {
            free(machine->transition_table[s]);
        }
        free(machine->transition_table);
        machine->transition_table = NULL;
    }
    if (machine && machine->accepting_states) {
        free(machine->accepting_states);
        machine->accepting_states = NULL;
    }
}

/*
 * Create the exact 15-state, 2-symbol Universal Turing Machine U(15,2)
 * Reference: Neary & Woods (2009), "Four Small Universal Turing Machines", Table 16
 */
struct turing_machine create_u15_2_machine(void) {
    struct turing_machine m = create_turing_machine(15, 1, 'c');

    /* Halt state is state 0 (STATE_HALT) */
    m.num_accepting_states = 1;
    m.accepting_states = (int*)malloc(sizeof(int));
    m.accepting_states[0] = STATE_HALT;

    struct transition_result** t = m.transition_table;

    /* u1 */
    t[1]['c'] = (struct transition_result){ .control_state = 2, .write_symbol = 'c', .dir = DIR_RIGHT };
    t[1]['b'] = (struct transition_result){ .control_state = 1, .write_symbol = 'b', .dir = DIR_RIGHT };

    /* u2 */
    t[2]['c'] = (struct transition_result){ .control_state = 3, .write_symbol = 'b', .dir = DIR_RIGHT };
    t[2]['b'] = (struct transition_result){ .control_state = 1, .write_symbol = 'b', .dir = DIR_RIGHT };

    /* u3 */
    t[3]['c'] = (struct transition_result){ .control_state = 7, .write_symbol = 'c', .dir = DIR_LEFT };
    t[3]['b'] = (struct transition_result){ .control_state = 5, .write_symbol = 'c', .dir = DIR_LEFT };

    /* u4 */
    t[4]['c'] = (struct transition_result){ .control_state = 6, .write_symbol = 'c', .dir = DIR_LEFT };
    t[4]['b'] = (struct transition_result){ .control_state = 5, .write_symbol = 'b', .dir = DIR_LEFT };

    /* u5 */
    t[5]['c'] = (struct transition_result){ .control_state = 1, .write_symbol = 'b', .dir = DIR_RIGHT };
    t[5]['b'] = (struct transition_result){ .control_state = 4, .write_symbol = 'b', .dir = DIR_LEFT };

    /* u6 */
    t[6]['c'] = (struct transition_result){ .control_state = 4, .write_symbol = 'b', .dir = DIR_LEFT };
    t[6]['b'] = (struct transition_result){ .control_state = 4, .write_symbol = 'b', .dir = DIR_LEFT };

    /* u7 */
    t[7]['c'] = (struct transition_result){ .control_state = 8, .write_symbol = 'c', .dir = DIR_LEFT };
    t[7]['b'] = (struct transition_result){ .control_state = 7, .write_symbol = 'b', .dir = DIR_LEFT };

    /* u8 */
    t[8]['c'] = (struct transition_result){ .control_state = 9, .write_symbol = 'b', .dir = DIR_LEFT };
    t[8]['b'] = (struct transition_result){ .control_state = 7, .write_symbol = 'b', .dir = DIR_LEFT };

    /* u9 */
    t[9]['c'] = (struct transition_result){ .control_state = 1, .write_symbol = 'c', .dir = DIR_RIGHT };
    t[9]['b'] = (struct transition_result){ .control_state = 10, .write_symbol = 'b', .dir = DIR_LEFT };

    /* u10: (u10, c) -> b L u11; (u10, b) -> HALT */
    t[10]['c'] = (struct transition_result){ .control_state = 11, .write_symbol = 'b', .dir = DIR_LEFT };
    t[10]['b'] = (struct transition_result){ .control_state = STATE_HALT, .write_symbol = 'b', .dir = DIR_RIGHT };

    /* u11 */
    t[11]['c'] = (struct transition_result){ .control_state = 12, .write_symbol = 'c', .dir = DIR_RIGHT };
    t[11]['b'] = (struct transition_result){ .control_state = 14, .write_symbol = 'b', .dir = DIR_RIGHT };

    /* u12 */
    t[12]['c'] = (struct transition_result){ .control_state = 13, .write_symbol = 'c', .dir = DIR_RIGHT };
    t[12]['b'] = (struct transition_result){ .control_state = 12, .write_symbol = 'b', .dir = DIR_RIGHT };

    /* u13 */
    t[13]['c'] = (struct transition_result){ .control_state = 2, .write_symbol = 'c', .dir = DIR_LEFT };
    t[13]['b'] = (struct transition_result){ .control_state = 12, .write_symbol = 'b', .dir = DIR_RIGHT };

    /* u14 */
    t[14]['c'] = (struct transition_result){ .control_state = 3, .write_symbol = 'c', .dir = DIR_LEFT };
    t[14]['b'] = (struct transition_result){ .control_state = 15, .write_symbol = 'c', .dir = DIR_RIGHT };

    /* u15 */
    t[15]['c'] = (struct transition_result){ .control_state = 14, .write_symbol = 'c', .dir = DIR_RIGHT };
    t[15]['b'] = (struct transition_result){ .control_state = 14, .write_symbol = 'b', .dir = DIR_RIGHT };

    return m;
}

int step_turing_machine(const struct turing_machine* machine, struct turing_machine_state* state) {
    if (state->control_state <= 0 || state->control_state > machine->num_states) {
        return 0; /* Halted or invalid */
    }

    ensure_tape_range(state, state->head_position, machine->blank_symbol);
    long long tape_idx = state->offset + state->head_position;
    symbol read_sym = state->tape[tape_idx];

    struct transition_result next = machine->transition_table[state->control_state][(unsigned char)read_sym];
    if (next.control_state == STATE_INVALID) {
        return -1; /* Undefined transition -> error / halt */
    }
    if (next.control_state == STATE_HALT) {
        state->control_state = STATE_HALT;
        return 0; /* Normal halt */
    }

    update_state(state, next.control_state, next.dir, next.write_symbol, machine->blank_symbol);
    return 1; /* Continued */
}

long long simulate(
    const struct turing_machine* machine,
    struct turing_machine_state* state,
    long long max_steps,
    int verbose
) {
    long long step = 0;
    if (verbose) {
        printf("--- Initial Configuration ---\n");
        trace_state(state, machine->blank_symbol);
    }

    while (state->control_state > 0 && state->control_state <= machine->num_states) {
        if (max_steps > 0 && step >= max_steps) {
            if (verbose) printf("Step limit (%lld) reached.\n", max_steps);
            return -1;
        }

        int res = step_turing_machine(machine, state);
        step++;

        if (verbose && (step <= 50 || step % 1000 == 0 || res <= 0)) {
            printf("\n[Step %lld]\n", step);
            trace_state(state, machine->blank_symbol);
        }

        if (res == 0) {
            if (verbose) printf("Machine HALTED successfully at step %lld.\n", step);
            return step;
        }
        if (res == -1) {
            if (verbose) printf("Machine HALTED on undefined transition at step %lld.\n", step);
            return step;
        }
    }

    return step;
}
