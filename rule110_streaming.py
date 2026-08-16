import ctypes
import numpy as np
import os
import itertools
import multiprocessing
import pickle
import time
from tqdm import tqdm  # Requires: pip install tqdm

# ==========================================
# 1. C Library Loading & Wrapper Definition
# ==========================================

lib_name = "./rule110.so"
if os.name == 'nt':
    lib_name = "./rule110.dll"

try:
    c_lib = ctypes.CDLL(lib_name)
    c_lib.create_engine.argtypes = [ctypes.c_int, ctypes.c_int]
    c_lib.create_engine.restype = ctypes.c_void_p
    c_lib.free_engine.argtypes = [ctypes.c_void_p]
    c_lib.free_engine.restype = None
    c_lib.run_simulation.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_int
    ]
    c_lib.run_simulation.restype = ctypes.c_int
except OSError:
    print(f"Error: Could not load {lib_name}. Make sure you compiled the C code.")
    exit(1)


class FastRule110:
    def __init__(self, width=512, maximum_steps=10000):
        if width % 64 != 0:
            raise ValueError("Width must be a multiple of 64.")

        self.width = width
        self.maximum_steps = maximum_steps
        self.engine_ptr = c_lib.create_engine(width, maximum_steps)
        self.input_buffer = np.zeros(width, dtype=np.uint8)
        self.output_buffer = np.zeros(width, dtype=np.uint8)
        self.input_ptr = self.input_buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        self.output_ptr = self.output_buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))

    def run(self, program_list):
        self.input_buffer.fill(0)
        n = len(program_list)
        if n > self.width: n = self.width
        reversed_program = program_list[::-1]
        self.input_buffer[-n:] = reversed_program

        steps = c_lib.run_simulation(self.engine_ptr, self.input_ptr, self.output_ptr, 0)

        if steps != -1:
            return np.packbits(self.output_buffer).tobytes(), steps
        else:
            return b'', -1

    def __del__(self):
        if hasattr(self, 'engine_ptr') and self.engine_ptr:
            c_lib.free_engine(self.engine_ptr)


# ==========================================
# 2. Worker Functions
# ==========================================

worker_engine = None


def worker_init(width, max_steps):
    global worker_engine
    worker_engine = FastRule110(width=width, maximum_steps=max_steps)


def process_batch(programs):
    results = []
    for prog in programs:
        out_bytes, steps = worker_engine.run(prog)
        results.append((tuple(prog), out_bytes, steps))
    return results


# ==========================================
# 3. Main Driver
# ==========================================

def main():
    # --- Configuration ---
    MAX_PROGRAM_LENGTH = 24
    OUTPUT_FILE = f"rule110_stream_L{MAX_PROGRAM_LENGTH}.pkl"
    WIDTH = 512
    MAX_STEPS = 100_000
    BATCH_SIZE = 1000

    # Calculate exact total for progress bar: Sum(2^i) for i=0 to L = 2^(L+1) - 1
    TOTAL_TASKS = (2 ** (MAX_PROGRAM_LENGTH + 1)) - 1
    # ---------------------

    print(f"--- Rule 110 Search (Streaming) ---")
    print(f"Max Program Length: {MAX_PROGRAM_LENGTH}")
    print(f"Total Simulations:  {TOTAL_TASKS:,}")
    print(f"Output File:        {OUTPUT_FILE}")
    print(f"CPU Cores:          {multiprocessing.cpu_count()}")
    print("-" * 30)

    # 1. Generator
    def program_generator():
        for length in range(0, MAX_PROGRAM_LENGTH + 1):
            # Optim: using product is fine, but for huge lengths custom bit manipulation is faster.
            # For <25, product is totally fine.
            for p_tuple in itertools.product([0, 1], repeat=length):
                yield list(p_tuple) + [1]

    # 2. Batch Generator
    def batch_generator(iterable, n):
        batch = []
        for item in iterable:
            batch.append(item)
            if len(batch) == n:
                yield batch
                batch = []
        if batch:
            yield batch

    # 3. Setup Pool
    # Reserve 2 cores for system/overhead if you have many, otherwise just -1
    num_workers = max(1, multiprocessing.cpu_count() - 2)
    pool = multiprocessing.Pool(
        processes=num_workers,
        initializer=worker_init,
        initargs=(WIDTH, MAX_STEPS)
    )

    t0 = time.time()

    # We open the file in 'append binary' or 'write binary' mode.
    # 'wb' overwrites existing file.
    with open(OUTPUT_FILE, "wb") as f_out:
        try:
            batches = batch_generator(program_generator(), BATCH_SIZE)

            # TQDM progress bar wraps the iterator
            # We assume each batch has BATCH_SIZE, but the last one might be smaller.
            # We update the progress bar manually to be accurate.

            with tqdm(total=TOTAL_TASKS, unit="sims", smoothing=0.1) as pbar:
                for batch_results in pool.imap_unordered(process_batch, batches):

                    # Write batch to file immediately
                    for prog_tuple, out_bytes, steps in batch_results:
                        # We save a tuple: (Program, OutputBytes, Steps)
                        pickle.dump((prog_tuple, out_bytes, steps), f_out)

                    # Update progress bar by the number of items actually processed in this batch
                    pbar.update(len(batch_results))

        except KeyboardInterrupt:
            print("\nStopping early...")
            pool.terminate()
            pool.join()
            print("File buffer flushed. Partial data saved.")
            return  # Exit cleanly
        else:
            pool.close()
            pool.join()

    duration = time.time() - t0
    print(f"\nCompleted in {duration:.2f} seconds.")
    print(f"Saved to {OUTPUT_FILE}")


if __name__ == '__main__':
    multiprocessing.freeze_support()
    main()