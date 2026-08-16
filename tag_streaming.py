import ctypes
import os
import itertools
import multiprocessing
import pickle
import time
from tqdm import tqdm

# --- Configuration ---
MIN_LEN = 3
MAX_LEN = 13  # Complexity grows exponentially (3^L)
BATCH_SIZE = 10000  # Number of rules sent to a worker at once
MAX_STEPS = 100_000  # Stop simulation after this many steps

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LIB_PATH = os.path.join(BASE_DIR, "tag_engine.so")
RESULTS_DIR = os.path.join(BASE_DIR, "results")
OUTPUT_FILE = os.path.join(RESULTS_DIR, f"tag_results_len{MAX_LEN}.pkl")

# --- Global Library Reference (for Workers) ---
tag_lib = None


def init_worker():
    """
    Called when a new worker process is created.
    Loads the C library into the process memory.
    """
    global tag_lib
    if not os.path.exists(LIB_PATH):
        raise FileNotFoundError(f"Could not find {LIB_PATH}. Compile the C code first.")

    tag_lib = ctypes.CDLL(LIB_PATH)

    # Define argtypes
    tag_lib.run_simulation.argtypes = [
        ctypes.c_char_p,                    # rule_a
        ctypes.c_char_p,                    # rule_b
        ctypes.c_char_p,                    # rule_c
        ctypes.c_ulonglong,                 # max_steps
        ctypes.c_char_p,                    # out_buf (NULL if not requested)
        ctypes.c_int,                       # out_buf_size
        ctypes.POINTER(ctypes.c_ulonglong), # out_steps
        ctypes.POINTER(ctypes.c_int)        # out_status
    ]
    tag_lib.run_simulation.restype = None


def worker_task(batch):
    """
    Processes a batch of (prog_idx, (ra, rb, rc)) rules.
    Returns:
        (halting_results: list[tuple[int, int]], non_halting_count: int)
        where each halting result is (program_index, runtime_steps).
    """
    halting_results = []
    non_halting_count = 0

    out_steps = ctypes.c_ulonglong(0)
    out_status = ctypes.c_int(0)

    for prog_idx, (ra, rb, rc) in batch:
        tag_lib.run_simulation(
            ra.encode('utf-8'),
            rb.encode('utf-8'),
            rc.encode('utf-8'),
            MAX_STEPS,
            None,  # No string output buffer needed -> skips linearization for maximum speed and minimum memory
            0,
            ctypes.byref(out_steps),
            ctypes.byref(out_status)
        )

        if out_status.value == 1:
            # Halted: store compact tuple (program_index, runtime)
            halting_results.append((prog_idx, out_steps.value))
        else:
            non_halting_count += 1

    return halting_results, non_halting_count


# --- Generator Logic ---

def count_total_rules(min_len, max_len):
    """Calculates total number of rule variants from min_len to max_len."""
    total = 0
    for l in range(min_len, max_len + 1):
        partitions = (l - 1) * (l - 2) // 2
        total += (3 ** l) * partitions * 3
    return total


def generate_partitions(s):
    """
    Yields all partitions of string s into 3 non-empty parts (u, v, w).
    """
    n = len(s)
    for i in range(1, n - 1):
        for j in range(i + 1, n):
            yield (s[:i], s[i:j], s[j:])


def rule_generator():
    """
    Yields batches of (prog_idx, (ra, rb, rc)) rules to process.
    Iterates length, then combinations, then partitions, then H-variants.
    """
    batch = []
    prog_idx = 0

    for length in range(MIN_LEN, MAX_LEN + 1):
        for chars in itertools.product('abc', repeat=length):
            base_str = "".join(chars)

            for (r1, r2, r3) in generate_partitions(base_str):
                variants = [
                    (r1 + "H", r2, r3),
                    (r1, r2 + "H", r3),
                    (r1, r2, r3 + "H")
                ]

                for variant in variants:
                    batch.append((prog_idx, variant))
                    prog_idx += 1

                    if len(batch) >= BATCH_SIZE:
                        yield batch
                        batch = []

    if batch:
        yield batch


# --- Main Execution ---

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    total_rules = count_total_rules(MIN_LEN, MAX_LEN)
    output_file = OUTPUT_FILE

    print(f"--- 2-Tag System Exhaustive Search ---")
    print(f"Rule lengths:            {MIN_LEN} to {MAX_LEN}")
    print(f"Total simulations:       {total_rules:,}")
    print(f"Output file (.pkl):      {output_file}")

    cores = multiprocessing.cpu_count()
    print(f"CPU Cores:               {cores}")
    print("-" * 40)

    halting_count = 0
    non_halting_count = 0
    start_time = time.time()

    with open(output_file, "wb") as f:
        with multiprocessing.Pool(processes=cores, initializer=init_worker) as pool:
            with tqdm(total=total_rules, desc="Simulating Tag systems", unit="sims", smoothing=0.1) as pbar:
                for halting_batch, non_halt_batch_count in pool.imap_unordered(worker_task, rule_generator()):
                    for res in halting_batch:
                        # Dumps compact (program_index, runtime) tuple
                        pickle.dump(res, f)

                    halting_count += len(halting_batch)
                    non_halting_count += non_halt_batch_count
                    pbar.update(len(halting_batch) + non_halt_batch_count)

    elapsed = time.time() - start_time
    total_processed = halting_count + non_halting_count
    rate = total_processed / elapsed if elapsed > 0 else 0
    file_size_mb = os.path.getsize(output_file) / (1024 * 1024) if os.path.exists(output_file) else 0

    print(f"\n--- Search Complete ---")
    print(f"Total processed:         {total_processed:,}")
    print(f"Halting programs logged: {halting_count:,} ({100 * halting_count / total_processed:.2f}%)")
    print(f"Non-halting programs:    {non_halting_count:,} ({100 * non_halting_count / total_processed:.2f}%)")
    print(f"Elapsed time:            {elapsed:.2f} seconds ({rate:,.0f} sims/sec)")
    print(f"Output file size:        {file_size_mb:.2f} MB")
    print(f"Results saved to '{output_file}'.")


def read_results(filename=OUTPUT_FILE, max_records=5):
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
                    print(f"Record {count}: index={record[0]}, runtime={record[1]}")
                count += 1
            except EOFError:
                break
        print(f"... Total halting records verified: {count:,}")


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()