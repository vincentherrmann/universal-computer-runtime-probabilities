#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK_CAPACITY 16384

typedef struct {
    char* data;
    size_t head;
    size_t tail;
    size_t capacity;
    size_t mask;
    size_t count;
    int is_dynamic;
} FastCircularBuffer;

static inline void init_fcb(FastCircularBuffer* cb, char* stack_buf, size_t stack_cap) {
    cb->data = stack_buf;
    cb->head = 0;
    cb->tail = 0;
    cb->capacity = stack_cap;
    cb->mask = stack_cap - 1;
    cb->count = 0;
    cb->is_dynamic = 0;
}

static inline void free_fcb(FastCircularBuffer* cb) {
    if (cb->is_dynamic && cb->data) {
        free(cb->data);
    }
}

static void resize_fcb(FastCircularBuffer* cb, size_t needed) {
    size_t new_cap = cb->capacity * 2;
    while (new_cap < cb->count + needed) {
        new_cap *= 2;
    }

    char* new_data = (char*)malloc(new_cap);

    // Linearize existing data into new buffer
    size_t part1 = (cb->capacity - cb->head < cb->count) ? (cb->capacity - cb->head) : cb->count;
    memcpy(new_data, cb->data + cb->head, part1);
    if (part1 < cb->count) {
        memcpy(new_data + part1, cb->data, cb->count - part1);
    }

    if (cb->is_dynamic) {
        free(cb->data);
    }

    cb->data = new_data;
    cb->head = 0;
    cb->tail = cb->count;
    cb->capacity = new_cap;
    cb->mask = new_cap - 1;
    cb->is_dynamic = 1;
}

static inline void append_rule(FastCircularBuffer* cb, const char* str, size_t len) {
    if (cb->count + len >= cb->capacity) {
        resize_fcb(cb, len);
    }

    size_t tail = cb->tail;
    size_t cap = cb->capacity;
    if (tail + len <= cap) {
        memcpy(cb->data + tail, str, len);
        cb->tail = (tail + len) & cb->mask;
    } else {
        size_t part1 = cap - tail;
        memcpy(cb->data + tail, str, part1);
        memcpy(cb->data, str + part1, len - part1);
        cb->tail = len - part1;
    }
    cb->count += len;
}

void run_simulation(
    const char* rule_a,
    const char* rule_b,
    const char* rule_c,
    unsigned long long max_steps,
    char* out_buf,
    int out_buf_size,
    unsigned long long* out_steps,
    int* out_status
) {
    char stack_buf[STACK_CAPACITY];
    FastCircularBuffer cb;
    init_fcb(&cb, stack_buf, STACK_CAPACITY);

    // Initial word "aaa"
    append_rule(&cb, "aaa", 3);

    size_t len_a = strlen(rule_a);
    size_t len_b = strlen(rule_b);
    size_t len_c = strlen(rule_c);

    unsigned long long steps = 0;
    int status = 0; // 0=MaxSteps, 1=Halted

    while (steps < max_steps) {
        // Halt condition 1: Length < 2 (standard 2-tag definition)
        if (cb.count < 2) {
            status = 1;
            break;
        }

        // Peek head
        char head = cb.data[cb.head];

        // Halt condition 2: Head is 'H'
        if (head == 'H') {
            status = 1;
            break;
        }

        // Remove 2 items
        cb.head = (cb.head + 2) & cb.mask;
        cb.count -= 2;

        // Append based on head
        if (head == 'a') append_rule(&cb, rule_a, len_a);
        else if (head == 'b') append_rule(&cb, rule_b, len_b);
        else if (head == 'c') append_rule(&cb, rule_c, len_c);

        steps++;
    }

    // Write output to python buffer if requested
    *out_steps = steps;
    *out_status = status;

    // Linearize buffer to output if out_buf is provided
    if (out_buf && out_buf_size > 0) {
        size_t write_len = (cb.count < (size_t)(out_buf_size - 1)) ? cb.count : (size_t)(out_buf_size - 1);
        for (size_t i = 0; i < write_len; i++) {
            out_buf[i] = cb.data[(cb.head + i) & cb.mask];
        }
        out_buf[write_len] = '\0'; // Null terminate
    }

    free_fcb(&cb);
}