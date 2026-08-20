import os
import sys
import time
import pickle
import ctypes
import itertools
import multiprocessing
from tqdm import tqdm

# ==========================================
# 1. Ctypes Wrapper for High-Performance UTM Engine
# ==========================================

_lib_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "utm_engine.so")

if not os.path.exists(_lib_path):
    # Compile shared library if not already present
    compile_cmd = f"gcc -O3 -march=native -funroll-loops -fPIC -shared {os.path.join(os.path.dirname(os.path.abspath(__file__)), 'utm_engine.c')} -o {_lib_path}"
    ret = os.system(compile_cmd)
    if ret != 0:
        raise RuntimeError(f"Failed to compile utm_engine.so with command: {compile_cmd}")

_lib = ctypes.CDLL(_lib_path)
_lib.create_utm_engine.argtypes = [ctypes.c_int]
_lib.create_utm_engine.restype = ctypes.c_void_p
_lib.free_utm_engine.argtypes = [ctypes.c_void_p]
_lib.run_utm_left.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_int,
    ctypes.c_int
]
_lib.run_utm_left.restype = ctypes.c_int


class FastUTM15_2:
    """
    High-performance C engine wrapper for the 15-state, 2-symbol
    Universal Turing Machine U(15,2) (Neary & Woods 2009).
    """
    def __init__(self, maximum_steps=100_000):
        self.maximum_steps = maximum_steps
        self.engine = _lib.create_utm_engine(maximum_steps)

    def __del__(self):
        if hasattr(self, "engine") and self.engine:
            _lib.free_utm_engine(self.engine)
            self.engine = None

    def run(self, program, initial_state=1):
        """
        Runs the simulation with the given binary program (list or tuple of 0/1)
        placed to the LEFT of the head at x in [-len(program), -1].
        Head starts at x = 0 in state u1 reading blank (c=0).
        Returns the step count when halted, or -1 if maximum_steps was reached.
        """
        length = len(program)
        c_prog = (ctypes.c_uint8 * length)(*program)
        return _lib.run_utm_left(self.engine, c_prog, length, initial_state)


# ==========================================
# 2. Multiprocessing Worker
# ==========================================

_worker_engine = None


def worker_init(maximum_steps):
    global _worker_engine
    _worker_engine = FastUTM15_2(maximum_steps=maximum_steps)


def process_batch(batch):
    """
    Processes a batch of binary programs.
    Returns (halting_results, non_halting_count).
    """
    global _worker_engine
    halting_results = []
    non_halting_count = 0

    for prog in batch:
        # Program placed at x in [-len, -1], head starts at x=0 in state u1
        steps = _worker_engine.run(prog, initial_state=1)

        if steps != -1:
            halting_results.append((len(prog), steps))
        else:
            non_halting_count += 1

    return halting_results, non_halting_count


# ==========================================
# 3. Generators
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


# ==========================================
# 4. Main Driver
# ==========================================

def main():
    # CLI quick test flag
    if len(sys.argv) > 1 and sys.argv[1] in ("--test", "-t"):
        print("--- Testing FastUTM15_2 Engine (Program to left of head) ---")
        eng = FastUTM15_2(maximum_steps=100_000)
        test_prog = [1, 1, 0, 0, 1]
        steps = eng.run(test_prog, initial_state=1)
        print(f"Test program {test_prog} (at x in [-5, -1]): runtime={steps} steps (halted={steps != -1})")
        return

    # --- Configuration ---
    MAX_PROGRAM_LENGTH = 30  # sum(2^L for L=1..19) = 1,048,574 (~1 million programs)
    MAX_STEPS = 100_000
    BATCH_SIZE = 5000

    os.makedirs("results", exist_ok=True)
    OUTPUT_FILE = os.path.join("results", f"utm15_2_results_len{MAX_PROGRAM_LENGTH}.pkl")

    TOTAL_TASKS = (2 ** (MAX_PROGRAM_LENGTH + 1)) - 2
    # ---------------------

    print(f"--- Universal Turing Machine U(15,2) Exhaustive Search ---")
    print(f"Machine:            15 states, 2 symbols (Neary & Woods 2009)")
    print(f"Blank Symbol:       c (0)")
    print(f"Initial State:      u1")
    print(f"Max Steps Limit:    {MAX_STEPS:,}")
    print(f"Max Program Length: {MAX_PROGRAM_LENGTH}")
    print(f"Total Simulations:  {TOTAL_TASKS:,}")
    print(f"Output File (.pkl): {OUTPUT_FILE}")
    print(f"CPU Cores:          {multiprocessing.cpu_count()}")
    print("-" * 58)

    num_workers = max(1, multiprocessing.cpu_count() - 2)
    pool = multiprocessing.Pool(
        processes=num_workers,
        initializer=worker_init,
        initargs=(MAX_STEPS,)
    )

    t0 = time.time()
    halting_count = 0
    non_halting_count = 0

    with open(OUTPUT_FILE, "wb") as f_out:
        try:
            batches = batch_generator(program_generator(MAX_PROGRAM_LENGTH), BATCH_SIZE)

            with tqdm(total=TOTAL_TASKS, desc="Simulating U(15,2)", unit="prog", smoothing=0.1) as pbar:
                for halting_batch, non_halt_batch_count in pool.imap_unordered(process_batch, batches):
                    if halting_batch:
                        pickle.dump(halting_batch, f_out)
                        halting_count += len(halting_batch)

                    non_halting_count += non_halt_batch_count
                    pbar.update(len(halting_batch) + non_halt_batch_count)

        except KeyboardInterrupt:
            print("\nExecution interrupted by user. Finalizing records...")
        finally:
            pool.close()
            pool.join()

    t1 = time.time()
    elapsed = t1 - t0
    total_processed = halting_count + non_halting_count
    rate = total_processed / elapsed if elapsed > 0 else 0

    print("\n" + "=" * 58)
    print("                 SIMULATION SUMMARY")
    print("=" * 58)
    print(f"Total Programs Processed: {total_processed:,} / {TOTAL_TASKS:,}")
    print(f"Halting Programs:         {halting_count:,} ({halting_count * 100.0 / total_processed:.2f}%)" if total_processed else "0")
    print(f"Non-Halting Programs:     {non_halting_count:,} ({non_halting_count * 100.0 / total_processed:.2f}%)" if total_processed else "0")
    print(f"Total Execution Time:     {elapsed:.2f} seconds ({elapsed / 60.0:.2f} minutes)")
    print(f"Overall Search Rate:      {rate:,.0f} simulations/sec")
    print(f"Results Saved To:         {OUTPUT_FILE}")
    print("=" * 58)


if __name__ == "__main__":
    main()
