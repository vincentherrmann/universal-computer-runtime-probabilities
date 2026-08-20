#ifndef _SIMULATE_TURING_MACHINE_H_
#define _SIMULATE_TURING_MACHINE_H_

#include <stddef.h>
#include <stdint.h>

#define TAPE_ALPHABET_SIZE 256
#define TRACE_TAPE_CHARS   80

#define STATE_HALT    0
#define STATE_INVALID -1

typedef char symbol;

enum direction {
    DIR_LEFT  = -1,
    DIR_RIGHT =  1
};

struct transition_result {
    int control_state;      /* Next state (1..num_states, or STATE_HALT) */
    symbol write_symbol;    /* Symbol to write */
    enum direction dir;     /* Head movement: DIR_LEFT or DIR_RIGHT */
};

struct turing_machine {
    int initial_control_state;
    symbol blank_symbol;
    int num_states;
    int num_accepting_states;
    int* accepting_states;
    struct transition_result** transition_table;
};

/* Two-sided infinite dynamic tape state */
struct turing_machine_state {
    int control_state;
    long long head_position;    /* Signed head index on the tape */
    long long min_index;        /* Lowest allocated tape coordinate */
    long long max_index;        /* Highest allocated tape coordinate */
    symbol* tape;               /* Allocated buffer */
    size_t capacity;            /* Total capacity in symbols */
    long long offset;           /* Array index corresponding to coordinate min_index */
};

/* Function Declarations */
struct turing_machine create_turing_machine(int num_states, int initial_state, symbol blank);
struct turing_machine create_u15_2_machine(void);
void free_turing_machine(struct turing_machine* machine);

struct turing_machine_state* create_initial_state(
    int initial_control_state,
    const char* input_string,
    long long initial_head_pos,
    symbol blank_symbol
);
void free_state(struct turing_machine_state* state);

void update_state(
    struct turing_machine_state* state,
    int new_control_state,
    enum direction dir,
    symbol write_symbol,
    symbol blank_symbol
);

void trace_state(const struct turing_machine_state* state, symbol blank_symbol);

int is_in_int_list(int value, int list_size, const int list[]);

/* Run one step. Returns 1 if active, 0 if halted, -1 if invalid transition */
int step_turing_machine(const struct turing_machine* machine, struct turing_machine_state* state);

/* Simulate until halt or max_steps. Returns step count if halted, -1 if step limit reached */
long long simulate(
    const struct turing_machine* machine,
    struct turing_machine_state* state,
    long long max_steps,
    int verbose
);

#endif /* _SIMULATE_TURING_MACHINE_H_ */
