# Asset-free regression tests

`tools/harness` runs the engine's real code without game assets, SDK or graphics, so a fix can carry a test that CI
runs on every change.

## Running them

    python -m pytest -q tools/harness

They need `clang` and the 32-bit C runtime (`lib32-glibc` on Arch, `gcc-multilib` on Debian and Ubuntu), as the game's
Linux build does; the Linux CI job has both. pytest finds every `test_*.py` in `tools/harness/tests`, so a new test needs no change to CI; each file
also runs on its own (`python tools/harness/tests/test_widget_pool.py`).

## How a test is made

A test is two files side by side in `tools/harness/tests`.

- **The C side**, `tools/harness/tests/<name>.c`: the fake world the code under test needs (the objects, clusters,
  tags or data arrays it reads, with recorders for what it produces) and the cases that drive it. It includes
  `harness.h` (the game's basic types, data arrays that refuse when full, `CHECK`), then its world, then
  `under_test.inc`, then a `main` that runs the case named on its command line. `CHECK(condition, "message", ...)`
  stops the case with the file, line, condition and message.
- **The Python side**, `tools/harness/tests/test_<name>.py`: takes the code under test straight from the sources (`function`,
  `inline`, `enum_with`, `constant`), so the test always checks what the game builds, and has `build` compile the
  two together for the game's 32-bit ABI. Each case is a pytest test.

**Negative controls.** Every test also runs the code under test with a deliberate fault (`mutated`: a line removed
or changed), and the named case must fail. A test that cannot fail proves nothing: writing these found a case that
compared the cache against itself. When a control stops applying (the line it changes is gone), it fails loudly.

The fake world needs only the members the code reads; a renamed member is a compile error, not a silent drift. Take
the game's enums and constants from the sources too (`enum_with`, `constant`) rather than copying their values, and
stub what the code calls as macros that name only the arguments they use, so an unrelated change to a signature
leaves the test alone.

## Examples

- `tests/test_object_bounds_cache.py`: collision's object loop over a crowded fake world, with and without a cache of the
  objects' bounding spheres; the features gathered compared in order.
- `tests/test_widget_pool.py`: the widget pool filling, refusing, freeing and reusing slots (64 slots, against 128
  players' assault rifles: the cause of invisible plasma bolts in big games).

`test_death_timing.py` and `test_light_storage.py` follow the same idea with their own helpers.

## Limits

Code is taken from the sources by matching braces: it holds for the functions taken so far, and a function that
cannot be found fails the test. Larger subsystems, with many dependencies, would be better compiled as whole source
files against stub headers than taken function by function.

These tests do not replace anything that needs the game's own data (levels, tags, scripts) or the whole game
running: that is checked by playing, or offline on real maps with the null renderer (`debug.null_renderer`), which
is also how a fake world is calibrated (the same hot spot, the same order of cost).
