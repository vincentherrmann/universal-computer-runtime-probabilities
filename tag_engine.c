#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define codes for fast lookup
// a=0, b=1, c=2, H=3
// ASCII: a=97, b=98, c=99, H=72

typedef struct {
    unsigned long long steps;
    int final_len;
    int halted; // 1 = Halted (H symbol or len < 2), 0 = Max steps reached
    // The actual string will be written to a passed buffer
} SimResult;

// Simple dynamic circular buffer
typedef struct {
    char* data;
    size_t head;
    size_t tail;
    size_t capacity;
    size_t count;
} CircularBuffer;

void init_cb(CircularBuffer* cb, size_t initial_cap) {
    cb->data = (char*)malloc(initial_cap);
    cb->head = 0;
    cb->tail = 0;
    cb->capacity = initial_cap;
    cb->count = 0;
}

void free_cb(CircularBuffer* cb) {
    if (cb->data) free(cb->data);
}

// Resizes buffer if needed
void append_str(CircularBuffer* cb, const char* str) {
    size_t len = strlen(str);

    // Resize if full
    if (cb->count + len >= cb->capacity) {
        size_t new_cap = cb->capacity * 2;
        if (new_cap < cb->count + len) new_cap = cb->count + len + 100;

        char* new_data = (char*)malloc(new_cap);

        // Copy data to new buffer in linear order
        size_t part1 = (cb->tail >= cb->head) ? cb->count : (cb->capacity - cb->head);
        memcpy(new_data, cb->data + cb->head, part1);
        if (part1 < cb->count) {
            memcpy(new_data + part1, cb->data, cb->count - part1);
        }

        free(cb->data);
        cb->data = new_data;
        cb->head = 0;
        cb->tail = cb->count;
        cb->capacity = new_cap;
    }

    // Append characters
    for (size_t i = 0; i < len; i++) {
        cb->data[cb->tail] = str[i];
        cb->tail = (cb->tail + 1) % cb->capacity;
    }
    cb->count += len;
}

char pop_front(CircularBuffer* cb) {
    if (cb->count == 0) return 0;
    char val = cb->data[cb->head];
    cb->head = (cb->head + 1) % cb->capacity;
    cb->count--;
    return val;
}

// The main exported function
// out_buf: A pre-allocated buffer from Python to hold the result string
// out_buf_size: Size of that buffer
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
    CircularBuffer cb;
    // Initial guess size 1KB, grows automatically
    init_cb(&cb, 1024);

    // Initial word "aaa"
    append_str(&cb, "aaa");

    unsigned long long steps = 0;
    int status = 0; // 0=MaxSteps, 1=Halted

    while (steps < max_steps) {
        // Halt condition 1: Length < 2 (standard 2-tag definition)
        if (cb.count < 2) {
            status = 1;
            break;
        }

        // Peek head (do not remove yet, usually we read head then remove 2)
        char head = cb.data[cb.head];

        // Halt condition 2: Head is 'H'
        if (head == 'H') {
            status = 1;
            break;
        }

        // Apply Rule
        // Remove 2 items
        pop_front(&cb);
        pop_front(&cb);

        // Append based on head
        if (head == 'a') append_str(&cb, rule_a);
        else if (head == 'b') append_str(&cb, rule_b);
        else if (head == 'c') append_str(&cb, rule_c);
        // If head was something else, we treat it as no-op or error, but here we assume clean input.

        steps++;
    }

    // Write output to python buffer
    *out_steps = steps;
    *out_status = status;

    // Linearize buffer to output
    size_t write_len = (cb.count < out_buf_size - 1) ? cb.count : out_buf_size - 1;
    for (size_t i = 0; i < write_len; i++) {
        out_buf[i] = cb.data[(cb.head + i) % cb.capacity];
    }
    out_buf[write_len] = '\0'; // Null terminate

    free_cb(&cb);
}