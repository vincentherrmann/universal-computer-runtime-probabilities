# File: bf_streaming.py

import ctypes
import os
import itertools
import multiprocessing
import pickle
import time
from tqdm import tqdm
from typing import Iterator

# --- Configuration ---
MAX_PROGRAM_LENGTH = 5
MAX_EXECUTION_STEPS = 100_000
BRAINFUCK_ALPHABET = "+-<>[]."  # Using the 7-char alphabet
MAX_OUTPUT_SIZE = 4096
BATCH_SIZE = 10000  # Number of programs sent to a worker at once

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LIB_PATH = os.path.join(BASE_DIR, "bf_interpreter.so")
RESULTS_DIR = os.path.join(BASE_DIR, "results")
OUTPUT_FILE_PKL = os.path.join(RESULTS_DIR, f"bf_results_len{MAX_PROGRAM_LENGTH}.pkl")

# --- Global Library Reference (for Workers) ---
bf_lib = None


def init_worker():
    """
    Called when a new worker process is created.
    Loads the C library into the worker process memory.
    """
    global bf_lib
    if not os.path.exists(LIB_PATH):
        raise FileNotFoundError(f"Could not find {LIB_PATH}. Compile the C code first.")

    bf_lib = ctypes.CDLL(LIB_PATH)
    bf_lib.execute_brainfuck.argtypes = [
        ctypes.c_char_p,                    # program
        ctypes.c_longlong,                  # max_steps
        ctypes.c_char_p,                    # out_buffer
        ctypes.c_size_t,                    # out_buffer_size
        ctypes.POINTER(ctypes.c_longlong),  # out_steps
        ctypes.POINTER(ctypes.c_int)        # out_status
    ]
    bf_lib.execute_brainfuck.restype = None


# --- Worker Simulation Function ---
def simulate_program(program: str):
    """
    Simulates a single Brainfuck program using the loaded C library.
    Returns:
        (halted: bool, result_dict or None)
    """
    program_bytes = program.encode('utf-8')
    output_buffer = ctypes.create_string_buffer(MAX_OUTPUT_SIZE)
    steps_taken = ctypes.c_longlong(0)
    status = ctypes.c_int(0)

    bf_lib.execute_brainfuck(
        program_bytes,
        MAX_EXECUTION_STEPS,
        output_buffer,
        MAX_OUTPUT_SIZE,
        ctypes.byref(steps_taken),
        ctypes.byref(status)
    )

    # status: 1 = Halted, 0 = Timeout, -1 = Runtime Error / Mismatched brackets
    if status.value == 1:
        raw_output = output_buffer.value
        output_str = raw_output.decode('utf-8', errors='replace') if len(raw_output) > 0 else None
        return True, {
            'program': program,
            'runtime': steps_taken.value,
            'steps': steps_taken.value,
            'output': output_str
        }
    return False, None


def worker_task(batch: list):
    """
    Processes a batch of programs.
    Returns:
        (halting_results: list[dict], non_halting_count: int)
    """
    halting_results = []
    non_halting_count = 0

    for prog in batch:
        halted, res = simulate_program(prog)
        if halted:
            halting_results.append(res)
        else:
            non_halting_count += 1

    return halting_results, non_halting_count


# --- Program & Batch Generators ---
def generate_programs_iterator(alphabet: str, max_len: int) -> Iterator[str]:
    """Yields all programs from length 1 to max_len without storing them in a list."""
    for length in range(1, max_len + 1):
        for p_tuple in itertools.product(alphabet, repeat=length):
            yield "".join(p_tuple)


def batch_generator(iterable, batch_size: int):
    """Batches an iterator into lists of size `batch_size`."""
    batch = []
    for item in iterable:
        batch.append(item)
        if len(batch) >= batch_size:
            yield batch
            batch = []
    if batch:
        yield batch


# --- Main Orchestration Script ---
def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    alphabet_size = len(BRAINFUCK_ALPHABET)
    total_programs = sum(alphabet_size ** i for i in range(1, MAX_PROGRAM_LENGTH + 1))

    print(f"--- Brainfuck Exhaustive Search ---")
    print(f"Max Program Length: {MAX_PROGRAM_LENGTH}")
    print(f"Alphabet:           '{BRAINFUCK_ALPHABET}' ({alphabet_size} symbols)")
    print(f"Total Programs:     {total_programs:,}")
    print(f"Output File (.pkl): {OUTPUT_FILE_PKL}")

    cpu_count = multiprocessing.cpu_count()
    print(f"CPU Cores:          {cpu_count}")
    print("-" * 40)

    halting_count = 0
    non_halting_count = 0
    start_time = time.time()

    program_gen = generate_programs_iterator(BRAINFUCK_ALPHABET, MAX_PROGRAM_LENGTH)
    batches = batch_generator(program_gen, BATCH_SIZE)

    with open(OUTPUT_FILE_PKL, 'wb') as f_out:
        with multiprocessing.Pool(processes=cpu_count, initializer=init_worker) as pool:
            with tqdm(total=total_programs, desc="Simulating BF programs", unit="prog") as pbar:
                for halting_batch, non_halt_batch_count in pool.imap_unordered(worker_task, batches):
                    for item in halting_batch:
                        pickle.dump(item, f_out)

                    halting_count += len(halting_batch)
                    non_halting_count += non_halt_batch_count
                    pbar.update(len(halting_batch) + non_halt_batch_count)

    elapsed = time.time() - start_time
    total_processed = halting_count + non_halting_count
    rate = total_processed / elapsed if elapsed > 0 else 0

    print(f"\n--- Search Complete ---")
    print(f"Total processed:         {total_processed:,}")
    print(f"Halting programs logged: {halting_count:,} ({100 * halting_count / total_processed:.2f}%)")
    print(f"Non-halting programs:    {non_halting_count:,} ({100 * non_halting_count / total_processed:.2f}%)")
    print(f"Elapsed time:            {elapsed:.2f} seconds ({rate:,.0f} progs/sec)")
    print(f"Results saved to '{OUTPUT_FILE_PKL}'.")


def read_results(filename=OUTPUT_FILE_PKL, max_records=5):
    """
    Helper to verify/read the pickle file.
    """
    print(f"\n--- Verifying Output ({filename}) ---")
    with open(filename, "rb") as f:
        count = 0
        while True:
            try:
                record = pickle.load(f)
                if count < max_records:
                    print(record)
                count += 1
            except EOFError:
                break
        print(f"... Total halting records verified: {count:,}")


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()