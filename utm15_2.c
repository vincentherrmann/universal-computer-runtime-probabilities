#include "simulate_turing_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void run_example_3_2(int verbose) {
    printf("=================================================================\n");
    printf("  Running U(15,2) Example 3.2 from Neary & Woods (2009)\n");
    printf("  Simulation of Production P(a1) on dataword a1 e_j a_i\n");
    printf("=================================================================\n");

    /*
     * Build Example 3.2 tape from page 16:
     * Prefix: ... c c c <P(a2)> (cb)^6 (cccb)^2 (cc)^3 cb cb cb bc
     * Head position is on the 'c' of 'bc' (rightmost symbol of G = bc)
     * Suffix: cb cb cb bb (cb)^8 (cb)^3 bb cc ...
     */
    char tape_str[4096];
    tape_str[0] = '\0';

    /* Prefix before head */
    strcat(tape_str, "cccccccc");
    for (int i = 0; i < 6; i++) strcat(tape_str, "cb");
    strcat(tape_str, "cccbcccb");
    for (int i = 0; i < 3; i++) strcat(tape_str, "cc");
    for (int i = 0; i < 3; i++) strcat(tape_str, "cb");
    strcat(tape_str, "bc");

    long long head_pos = (long long)strlen(tape_str) - 1; /* over the 'c' of 'bc' */

    /* Suffix after head */
    for (int i = 0; i < 3; i++) strcat(tape_str, "cb");
    strcat(tape_str, "bb");
    for (int i = 0; i < 8; i++) strcat(tape_str, "cb"); /* (cb)^8jq for jq=1 */
    for (int i = 0; i < 3; i++) strcat(tape_str, "cb"); /* (cb)^(8i-5) for i=1 -> (cb)^3 */
    strcat(tape_str, "bbcc");
    strcat(tape_str, "cccccccc");

    printf("Constructed Initial Tape (length %zu, head at position %lld):\n", strlen(tape_str), head_pos);
    printf("Tape: %s\n\n", tape_str);

    struct turing_machine u15_2 = create_u15_2_machine();
    struct turing_machine_state* state = create_initial_state(u15_2.initial_control_state, tape_str, head_pos, u15_2.blank_symbol);

    long long steps = simulate(&u15_2, state, 150, verbose);
    printf("\nExecuted %lld steps.\n", steps);

    free_state(state);
    free_turing_machine(&u15_2);
}

void run_halting_example(int verbose) {
    printf("=================================================================\n");
    printf("  Running U(15,2) Halting Configuration from Page 19\n");
    printf("  Leftmost symbol is encoded halt symbol e_h = (cb)^(8hq+3) bb\n");
    printf("=================================================================\n");

    /*
     * Build halting tape from page 19:
     * u1, bb cc cb <P(e_h-1, a_q)> cb ... (cb)^2 <P(a1)> (cb)^3 ((cc)^2)* bc (cb)^(8hq+3) bb (<A> bb)* cc cc ...
     */
    char tape_str[4096];
    tape_str[0] = '\0';

    strcat(tape_str, "cccc");
    strcat(tape_str, "bbcccb"); /* H = bbcccb */
    strcat(tape_str, "cb");     /* V = cb */
    for (int i = 0; i < 2; i++) strcat(tape_str, "cb");
    strcat(tape_str, "cccbcccb");
    for (int i = 0; i < 3; i++) strcat(tape_str, "cb");
    strcat(tape_str, "cccc");   /* S = (cc)^2 */
    strcat(tape_str, "bc");     /* G = bc, head is on 'c' */

    long long head_pos = (long long)strlen(tape_str) - 1;

    for (int i = 0; i < 11; i++) strcat(tape_str, "cb"); /* (cb)^(8hq+3) for hq=1 -> 11 */
    strcat(tape_str, "bb");
    for (int i = 0; i < 3; i++) strcat(tape_str, "cb");
    strcat(tape_str, "bbcccc");

    printf("Constructed Halting Tape (length %zu, head at position %lld):\n", strlen(tape_str), head_pos);
    printf("Tape: %s\n\n", tape_str);

    struct turing_machine u15_2 = create_u15_2_machine();
    struct turing_machine_state* state = create_initial_state(u15_2.initial_control_state, tape_str, head_pos, u15_2.blank_symbol);

    long long steps = simulate(&u15_2, state, 1000, verbose);
    printf("\nResult: Halting test finished at step %lld (State u%d).\n", steps, state->control_state);

    free_state(state);
    free_turing_machine(&u15_2);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0)) {
        run_example_3_2(1);
        printf("\n");
        run_halting_example(1);
        return 0;
    }

    if (argc < 2) {
        printf("Universal Turing Machine U(15,2) Simulator\n");
        printf("Reference: T. Neary & D. Woods (2009), 'Four Small Universal Turing Machines'\n\n");
        printf("Usage:\n");
        printf("  %s --test                           Run built-in verification test suite\n", argv[0]);
        printf("  %s <tape> [head_pos] [max_steps] [verbose]\n\n", argv[0]);
        printf("Example:\n");
        printf("  %s \"cbcbcccbcbc\" 5 1000 1\n", argv[0]);
        return 0;
    }

    const char* input_tape = argv[1];
    long long head_pos = (argc > 2) ? atoll(argv[2]) : 0;
    long long max_steps = (argc > 3) ? atoll(argv[3]) : 100000;
    int verbose = (argc > 4) ? atoi(argv[4]) : 1;

    struct turing_machine u15_2 = create_u15_2_machine();
    struct turing_machine_state* state = create_initial_state(u15_2.initial_control_state, input_tape, head_pos, u15_2.blank_symbol);

    printf("Simulating U(15,2) with %zu tape symbols, initial state u%d, head at %lld...\n", strlen(input_tape), u15_2.initial_control_state, head_pos);
    long long steps = simulate(&u15_2, state, max_steps, verbose);
    printf("\nExecution ended after %lld steps in state u%d.\n", steps, state->control_state);

    free_state(state);
    free_turing_machine(&u15_2);
    return 0;
}
