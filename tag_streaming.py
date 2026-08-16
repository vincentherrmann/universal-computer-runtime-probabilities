import ctypes
import os
import itertools
import multiprocessing
import pickle
import time

# --- Configuration ---
MIN_LEN = 3
MAX_LEN = 10  # Be careful: complexity grows exponentially (3^L)
BATCH_SIZE = 1000  # Number of rules sent to a worker at once
MAX_STEPS = 100000  # Stop simulation after this many steps

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LIB_PATH = os.path.join(BASE_DIR, "tag_engine.so")
RESULTS_DIR = os.path.join(BASE_DIR, "results")
OUTPUT_FILE = os.path.join(RESULTS_DIR, "tag_results.pkl")

# --- Global Library Reference (for Workers) ---
# We use a global variable so the DLL is loaded once per worker process
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
        ctypes.c_char_p,  # rule_a
        ctypes.c_char_p,  # rule_b
        ctypes.c_char_p,  # rule_c
        ctypes.c_ulonglong,  # max_steps
        ctypes.c_char_p,  # out_buf
        ctypes.c_int,  # out_buf_size
        ctypes.POINTER(ctypes.c_ulonglong),  # out_steps
        ctypes.POINTER(ctypes.c_int)  # out_status
    ]


def simulate_rule(ra, rb, rc):
    """
    Helper function to run a single simulation using the loaded C lib.
    """
    # Prepare buffers
    out_buf_size = 2048
    out_buf = ctypes.create_string_buffer(out_buf_size)
    out_steps = ctypes.c_ulonglong(0)
    out_status = ctypes.c_int(0)

    tag_lib.run_simulation(
        ra.encode('utf-8'),
        rb.encode('utf-8'),
        rc.encode('utf-8'),
        MAX_STEPS,
        out_buf,
        out_buf_size,
        ctypes.byref(out_steps),
        ctypes.byref(out_status)
    )

    return {
        'rules': (ra, rb, rc),
        'halted': bool(out_status.value),
        'steps': out_steps.value,
        # Only decode output if it halted (save space), otherwise it's just '...'
        'output': out_buf.value.decode('utf-8') if out_status.value else None
    }


def worker_task(rule_batch):
    """
    Processing function for the pool. Receives a batch of rules.
    """
    results = []
    for (ra, rb, rc) in rule_batch:
        results.append(simulate_rule(ra, rb, rc))
    return results


# --- Generator Logic ---

def generate_partitions(s):
    """
    Yields all partitions of string s into 3 non-empty parts (u, v, w).
    """
    n = len(s)
    # We need two cut points i and j such that:
    # 1 <= i < j < n
    # u = s[:i], v = s[i:j], w = s[j:]
    for i in range(1, n - 1):
        for j in range(i + 1, n):
            yield (s[:i], s[i:j], s[j:])


def rule_generator():
    """
    Yields batches of rules to process.
    Iterates length, then combinations, then partitions, then H-variants.
    """
    batch = []

    for length in range(MIN_LEN, MAX_LEN + 1):
        # Generate all strings of 'a', 'b', 'c' of this length
        for chars in itertools.product('abc', repeat=length):
            base_str = "".join(chars)

            # Partition into 3 rules
            for (r1, r2, r3) in generate_partitions(base_str):

                # Systematically append 'H' to one of the rules
                variants = [
                    (r1 + "H", r2, r3),
                    (r1, r2 + "H", r3),
                    (r1, r2, r3 + "H")
                ]

                for variant in variants:
                    batch.append(variant)

                    if len(batch) >= BATCH_SIZE:
                        yield batch
                        batch = []

    # Yield remaining
    if batch:
        yield batch


# --- Main Execution ---

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    output_file = OUTPUT_FILE
    print(f"Starting search. Lengths {MIN_LEN}-{MAX_LEN}. Output: {output_file}")

    # Use 'ab' to append binary. This creates a stream of pickle objects.
    # To read it back, you loop pickle.load(f) until EOF.
    count = 0
    halt_count = 0

    start_time = time.time()

    # Determine core count
    cores = multiprocessing.cpu_count()
    print(f"Using {cores} cores.")

    with multiprocessing.Pool(processes=cores, initializer=init_worker) as pool:
        with open(output_file, "ab") as f:

            # imap_unordered is best for streaming results as soon as they are ready
            # processing logic is in chunks, so result is a list of results
            for result_batch in pool.imap_unordered(worker_task, rule_generator()):

                for res in result_batch:
                    # Write to disk
                    pickle.dump(res, f)

                    count += 1
                    if res['halted']:
                        halt_count += 1

                # Progress update every ~100k
                if count % 100000 < BATCH_SIZE:
                    elapsed = time.time() - start_time
                    rate = count / elapsed
                    print(f"Processed: {count:,} | Halted: {halt_count:,} | Rate: {rate:.0f} sims/sec")

    print(f"Done. Total processed: {count}. Results saved to {output_file}.")


def read_results(filename=OUTPUT_FILE):
    """
    Helper to verify/read the pickle file.
    """
    print(f"\n--- Verifying Output ({filename}) ---")
    with open(filename, "rb") as f:
        try:
            for _ in range(5):  # Print first 5
                print(pickle.load(f))
            print("...")
        except EOFError:
            pass


if __name__ == "__main__":
    # Ensure this script is not imported recursively by workers
    main()
    # Uncomment below to verify the file after run
    # read_results()