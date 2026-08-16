// File: bf_interpreter.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAPE_SIZE 30000

// Tape static state, reset on each call.
static unsigned char tape[TAPE_SIZE] = {0};

// Function to pre-compute the jump locations for '[' and ']'
// Returns 0 on success, -1 on mismatched brackets.
int build_jump_map(const char* program, size_t prog_len, size_t* jump_map, size_t* stack) {
    int stack_ptr = -1;

    for (size_t i = 0; i < prog_len; ++i) {
        if (program[i] == '[') {
            stack_ptr++;
            stack[stack_ptr] = i;
        } else if (program[i] == ']') {
            if (stack_ptr < 0) {
                return -1; // Mismatched ']'
            }
            size_t start_pos = stack[stack_ptr];
            stack_ptr--;
            jump_map[start_pos] = i;
            jump_map[i] = start_pos;
        }
    }

    return (stack_ptr == -1) ? 0 : -1; // Success if stack is empty
}

// The modified interpreter function
// out_status: 1 = Halted successfully, 0 = Timeout (max steps reached), -1 = Runtime error / mismatched brackets
void execute_brainfuck(const char* program, long long max_steps,
                       char* out_buffer, size_t out_buffer_size,
                       long long* out_steps, int* out_status) {
    // ---- Initialization ----
    memset(tape, 0, TAPE_SIZE);
    *out_steps = 0;
    *out_status = 0;
    if (out_buffer_size > 0) out_buffer[0] = '\0';

    size_t prog_len = strlen(program);
    if (prog_len == 0) {
        *out_status = 1;
        return;
    }

    size_t stack_local[4096];
    size_t jump_map_local[4096];
    size_t* stack = stack_local;
    size_t* jump_map = jump_map_local;
    int need_free = 0;

    if (prog_len > 4096) {
        stack = (size_t*)malloc(prog_len * sizeof(size_t));
        jump_map = (size_t*)malloc(prog_len * sizeof(size_t));
        need_free = 1;
        if (!stack || !jump_map) {
            if (stack) free(stack);
            if (jump_map) free(jump_map);
            *out_status = -1;
            return;
        }
    }

    if (build_jump_map(program, prog_len, jump_map, stack) != 0) {
        if (need_free) {
            free(stack);
            free(jump_map);
        }
        *out_status = -1;
        return;
    }

    size_t ip = 0; // Instruction Pointer
    size_t dp = 0; // Data Pointer
    size_t output_len = 0;
    long long step_count = 0;
    int error = 0;

    // ---- Execution Loop ----
    while (ip < prog_len && step_count < max_steps) {
        char command = program[ip];
        switch (command) {
            case '>':
                if (dp + 1 >= TAPE_SIZE) {
                    error = 1;
                    goto end_execution;
                }
                dp++;
                break;
            case '<':
                if (dp == 0) {
                    error = 1;
                    goto end_execution;
                }
                dp--;
                break;
            case '+': tape[dp]++; break;
            case '-': tape[dp]--; break;
            case '.':
                if (output_len + 1 < out_buffer_size) {
                    out_buffer[output_len++] = (char)tape[dp];
                    out_buffer[output_len] = '\0'; // Keep it null-terminated
                }
                break;
            case ',': tape[dp] = 0; break;
            case '[': if (tape[dp] == 0) ip = jump_map[ip]; break;
            case ']': if (tape[dp] != 0) ip = jump_map[ip]; break;
        }
        ip++;
        step_count++;
    }

end_execution:
    *out_steps = step_count;
    if (need_free) {
        free(stack);
        free(jump_map);
    }

    if (error) {
        *out_status = -1;
    } else if (step_count >= max_steps) {
        *out_status = 0; // Timeout
    } else {
        *out_status = 1; // Halted
    }
}