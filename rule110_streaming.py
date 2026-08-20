import ctypes
import numpy as np
import os
import sys
import itertools
import multiprocessing
import pickle
import time
from tqdm import tqdm

# ==========================================
# 1. C Library Loading & Wrapper Definition
# ==========================================

lib_dir = os.path.dirname(os.path.abspath(__file__))
lib_name = os.path.join(lib_dir, "rule110.so")
if os.name == 'nt':
    lib_name = os.path.join(lib_dir, "rule110.dll")

try:
    c_lib = ctypes.CDLL(lib_name)
    c_lib.create_engine.argtypes = [ctypes.c_int, ctypes.c_int]
    c_lib.create_engine.restype = ctypes.c_void_p
    c_lib.free_engine.argtypes = [ctypes.c_void_p]
    c_lib.free_engine.restype = None
    c_lib.run_simulation.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_int
    ]
    c_lib.run_simulation.restype = ctypes.c_int
except OSError:
    print(f"Error: Could not load {lib_name}. Make sure you compiled the C code with:")
    print("  gcc -O3 -fPIC -shared rule110.c -o rule110.so")
    exit(1)


class FastRule110:
    def __init__(self, width=200_128, maximum_steps=100_000):
        if width < 2 * maximum_steps + 128:
            width = 2 * maximum_steps + 128
        if width % 64 != 0:
            width = ((width + 63) // 64) * 64

        self.width = width
        self.maximum_steps = maximum_steps
        self.engine_ptr = c_lib.create_engine(width, maximum_steps)

    def run(self, program_list, verbose=0):
        prog_arr = np.ascontiguousarray(program_list, dtype=np.uint8)
        prog_ptr = prog_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        prog_len = len(program_list)

        steps = c_lib.run_simulation(
            self.engine_ptr,
            prog_ptr,
            prog_len,
            None,  # No output state buffer needed during search
            verbose
        )

        return steps

    def __del__(self):
        if hasattr(self, 'engine_ptr') and self.engine_ptr:
            c_lib.free_engine(self.engine_ptr)
            self.engine_ptr = None


# ==========================================
# 2. Worker Functions
# ==========================================

worker_engine = None


def worker_init(width, max_steps):
    global worker_engine
    worker_engine = FastRule110(width=width, maximum_steps=max_steps)


def process_batch(programs):
    """
    Processes a batch of programs.
    Returns:
        (halting_results: list[dict], non_halting_count: int)
        where each halting result contains program, runtime, and length.
    """
    halting_results = []
    non_halting_count = 0

    for prog in programs:
        steps = worker_engine.run(prog)
        if steps != -1:
            # Halting program: record program, runtime, and length
            halting_results.append({
                'program': tuple(prog),
                'runtime': steps,
                'steps': steps,
                'length': len(prog)
            })
        else:
            non_halting_count += 1

    return halting_results, non_halting_count


# ==========================================
# 3. Main Driver
# ==========================================

def program_generator(max_len):
    """Yields all binary programs from length 1 to max_len."""
    for length in range(1, max_len + 1):
        for p_tuple in itertools.product([0, 1], repeat=length):
            yield list(p_tuple)


def batch_generator(iterable, n):
    """Batches an iterator into lists of size n."""
    batch = []
    for item in iterable:
        batch.append(item)
        if len(batch) >= n:
            yield batch
            batch = []
    if batch:
        yield batch


def main():
    # Simple CLI check for quick testing / visualization
    if len(sys.argv) > 1 and sys.argv[1] in ("--test", "-t"):
        steps_to_run = 150
        if len(sys.argv) > 2:
            try:
                steps_to_run = int(sys.argv[2])
            except ValueError:
                pass
        print(f"--- Running Test Simulation (Two-sided infinite ether tape, max_steps={steps_to_run}) ---")
        engine = FastRule110(width=128, maximum_steps=steps_to_run)
        test_prog = [1, 0, 1, 1, 0]
        print(f"Initial program at x=0..4: {test_prog}")
        steps = engine.run(test_prog, verbose=1)
        print(f"Result: runtime={steps}, length={len(test_prog)}, halted={steps != -1}")
        return

    # --- Configuration ---
    MAX_PROGRAM_LENGTH = 24
    MAX_STEPS = 100_000
    WIDTH = 2 * MAX_STEPS + 128  # 200,128 cells guarantees exact infinite tape for 100k steps
    BATCH_SIZE = 250  # Smaller batch size ensures smooth, frequent progress bar updates

    os.makedirs("results", exist_ok=True)
    OUTPUT_FILE = os.path.join("results", f"rule110_results_len{MAX_PROGRAM_LENGTH}.pkl")

    # Total programs from length 1 to MAX_PROGRAM_LENGTH: sum(2^L) = 2^(MAX_LEN+1) - 2
    TOTAL_TASKS = (2 ** (MAX_PROGRAM_LENGTH + 1)) - 2
    # ---------------------

    print(f"--- Rule 110 Exhaustive Search (Two-Sided Infinite Ether Tape) ---")
    print(f"Ether Pattern:      11111000100110 (14-bit Matthew Cook background)")
    print(f"Halting Interval:   [-7, n + 6] matches original ether pattern")
    print(f"Tape Width Window:  {WIDTH:,} cells (positions -{WIDTH//2} to +{WIDTH//2 - 1})")
    print(f"Max Steps Limit:    {MAX_STEPS:,}")
    print(f"Max Program Length: {MAX_PROGRAM_LENGTH}")
    print(f"Total Simulations:  {TOTAL_TASKS:,}")
    print(f"Output File (.pkl): {OUTPUT_FILE}")
    print(f"CPU Cores:          {multiprocessing.cpu_count()}")
    print("-" * 55)

    # Setup Pool
    num_workers = max(1, multiprocessing.cpu_count() - 2)
    pool = multiprocessing.Pool(
        processes=num_workers,
        initializer=worker_init,
        initargs=(WIDTH, MAX_STEPS)
    )

    t0 = time.time()
    halting_count = 0
    non_halting_count = 0

    with open(OUTPUT_FILE, "wb") as f_out:
        try:
            batches = batch_generator(program_generator(MAX_PROGRAM_LENGTH), BATCH_SIZE)

            with tqdm(total=TOTAL_TASKS, desc="Simulating Rule 110", unit="prog", smoothing=0.1) as pbar:
                for halting_batch, non_halt_batch_count in pool.imap_unordered(process_batch, batches):
                    for record in halting_batch:
                        # Log halting record: dict with program, runtime, and length
                        pickle.dump(record, f_out)

                    halting_count += len(halting_batch)
                    non_halting_count += non_halt_batch_count
                    pbar.update(len(halting_batch) + non_halt_batch_count)

        except KeyboardInterrupt:
            print("\nStopping early...")
            pool.terminate()
            pool.join()
            print("File buffer flushed. Partial data saved.")
            return
        else:
            pool.close()
            pool.join()

    duration = time.time() - t0
    total_processed = halting_count + non_halting_count
    rate = total_processed / duration if duration > 0 else 0

    print(f"\n--- Search Complete ---")
    print(f"Total processed:         {total_processed:,}")
    print(f"Halting programs logged: {halting_count:,} ({100 * halting_count / total_processed:.2f}%)")
    print(f"Non-halting programs:    {non_halting_count:,} ({100 * non_halting_count / total_processed:.2f}%)")
    print(f"Elapsed time:            {duration:.2f} seconds ({rate:,.0f} progs/sec)")
    print(f"Results saved to '{OUTPUT_FILE}'.")


if __name__ == '__main__':
    multiprocessing.freeze_support()
    main()