import ctypes
import os
import sys

# Load shared C library
_dir = os.path.dirname(os.path.abspath(__file__))
_so_path = os.path.join(_dir, "utm15_2.so")
if os.path.exists(_so_path):
    _lib = ctypes.CDLL(_so_path)
else:
    _lib = None


class TuringMachineResult(ctypes.Structure):
    _fields_ = [
        ("control_state", ctypes.c_int),
        ("write_symbol", ctypes.c_char),
        ("dir", ctypes.c_int),
    ]


class TuringMachine(ctypes.Structure):
    _fields_ = [
        ("initial_control_state", ctypes.c_int),
        ("blank_symbol", ctypes.c_char),
        ("num_states", ctypes.c_int),
        ("num_accepting_states", ctypes.c_int),
        ("accepting_states", ctypes.POINTER(ctypes.c_int)),
        ("transition_table", ctypes.POINTER(ctypes.POINTER(TuringMachineResult))),
    ]


class TuringMachineState(ctypes.Structure):
    _fields_ = [
        ("control_state", ctypes.c_int),
        ("head_position", ctypes.c_longlong),
        ("min_index", ctypes.c_longlong),
        ("max_index", ctypes.c_longlong),
        ("tape", ctypes.c_char_p),
        ("capacity", ctypes.c_size_t),
        ("offset", ctypes.c_longlong),
    ]


class UTM15_2:
    """
    Simulator for the 15-state, 2-symbol Universal Turing Machine U(15,2).
    Reference: Turlough Neary and Damien Woods (2009),
               'Four Small Universal Turing Machines', Fundamenta Informaticae 91, pp. 123-144.
    """

    TRANSITIONS = {
        # (state, symbol): (next_state, write_symbol, direction)
        (1, "c"): (2, "c", 1),
        (1, "b"): (1, "b", 1),
        (2, "c"): (3, "b", 1),
        (2, "b"): (1, "b", 1),
        (3, "c"): (7, "c", -1),
        (3, "b"): (5, "c", -1),
        (4, "c"): (6, "c", -1),
        (4, "b"): (5, "b", -1),
        (5, "c"): (1, "b", 1),
        (5, "b"): (4, "b", -1),
        (6, "c"): (4, "b", -1),
        (6, "b"): (4, "b", -1),
        (7, "c"): (8, "c", -1),
        (7, "b"): (7, "b", -1),
        (8, "c"): (9, "b", -1),
        (8, "b"): (7, "b", -1),
        (9, "c"): (1, "c", 1),
        (9, "b"): (10, "b", -1),
        (10, "c"): (11, "b", -1),
        # (10, 'b') is the undefined halt transition (STATE_HALT = 0)
        (11, "c"): (12, "c", 1),
        (11, "b"): (14, "b", 1),
        (12, "c"): (13, "c", 1),
        (12, "b"): (12, "b", 1),
        (13, "c"): (2, "c", -1),
        (13, "b"): (12, "b", 1),
        (14, "c"): (3, "c", -1),
        (14, "b"): (15, "c", 1),
        (15, "c"): (14, "c", 1),
        (15, "b"): (14, "b", 1),
    }

    def __init__(self, initial_tape="c", initial_head_pos=0, initial_state=1, blank_symbol="c"):
        self.blank = blank_symbol
        self.state = initial_state
        self.head_pos = initial_head_pos
        self.tape = {}
        for i, ch in enumerate(initial_tape):
            self.tape[i] = ch

    def step(self):
        """Executes a single step. Returns True if active, False if halted."""
        sym = self.tape.get(self.head_pos, self.blank)
        rule = self.TRANSITIONS.get((self.state, sym))
        if rule is None:
            return False  # Halted (undefined transition)

        next_state, write_sym, dir_val = rule
        self.tape[self.head_pos] = write_sym
        self.head_pos += dir_val
        self.state = next_state
        return self.state != 0

    def run(self, max_steps=100000, verbose=False):
        """Simulates until halt or max_steps. Returns total step count and halted boolean."""
        steps = 0
        if verbose:
            self.print_tape(step=0)

        while steps < max_steps:
            active = self.step()
            steps += 1
            if verbose and (steps <= 20 or steps % 1000 == 0 or not active):
                self.print_tape(step=steps)
            if not active:
                return steps, True  # Halted

        return steps, False  # Step limit reached

    def print_tape(self, step=0, window=35):
        """Prints diagnostic view of head and surrounding tape."""
        keys = self.tape.keys()
        min_k = min(keys) if keys else 0
        max_k = max(keys) if keys else 0

        view_start = self.head_pos - window
        view_end = self.head_pos + window

        tape_chars = [self.tape.get(p, self.blank) for p in range(view_start, view_end + 1)]
        head_indicator = ["v" if p == self.head_pos else " " for p in range(view_start, view_end + 1)]

        print(f"Step {step:6d} | State: u{self.state:<2d} | Head pos: {self.head_pos:+6d}")
        print(f"               [{''.join(head_indicator)}]")
        print(f"       Tape:   [{''.join(tape_chars)}]\n")


def encode_example_3_2():
    """Builds the Example 3.2 tape from Neary & Woods (2009)."""
    prefix = "cccccccc" + "cb" * 6 + "cccbcccb" + "cc" * 3 + "cb" * 3 + "bc"
    head_pos = len(prefix) - 1
    suffix = "cb" * 3 + "bb" + "cb" * 8 + "cb" * 3 + "bbcc" + "cccccccc"
    return prefix + suffix, head_pos


if __name__ == "__main__":
    print("--- Universal Turing Machine U(15,2) Python & C Engine ---")
    tape_str, head_pos = encode_example_3_2()
    print(f"Example 3.2 Tape Length: {len(tape_str)}, Initial Head Position: {head_pos}")

    sim = UTM15_2(initial_tape=tape_str, initial_head_pos=head_pos)
    steps, halted = sim.run(max_steps=50, verbose=True)
    print(f"Simulation completed {steps} steps (halted={halted}).")
