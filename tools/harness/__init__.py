"""Asset-free regression tests: the real engine code, compiled against a fake world.

A test has two halves. Its C file (tools/harness/tests/<name>.c) holds the fake world the code under test needs and
the cases that drive it, checked with CHECK (include/harness.h). Its Python file (tools/test_<name>.py) takes the code
under test straight from the sources (function, inline, enum_with, constant), so the test always checks what the game
builds, has build() compile the two together for the game's 32-bit ABI, and runs each case as a pytest test.

Every test also runs its negative controls: the code under test with a deliberate fault (mutate=), which a case must
catch. A test that cannot fail proves nothing. README.md in this folder walks through a test.
"""

import functools
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HARNESS = Path(__file__).resolve().parent


def read(relative):
    """A source file of the repository, as the compiler reads it."""
    return (ROOT / relative).read_text(encoding="latin-1")


def function(source, name):
    """A function's definition (static, inline or not), not its prototypes.

    Taken by matching braces, which holds for the engine's sources: no braces inside its strings or comments in
    the functions these tests take. A function that is not found fails the test at once.
    """
    match = re.search(r"^(?:static |__inline )?[\w *]+?\b" + re.escape(name) + r"\(\s*[^;{]*\)\s*\{", source, re.M)
    if not match:
        raise LookupError(f"function not found in the sources: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def inline(source, name):
    """An __inline header function, as a static one."""
    return function(source, name).replace("__inline", "static", 1)


def enum_with(source, member):
    """The whole enum that declares a member."""
    at = source.index(member)
    start = source.rindex("enum", 0, at)
    return source[start:source.index("};", at) + 2]


def constant(source, name):
    """An enum constant's or #define's integer value, as written."""
    match = re.search(r"\b" + re.escape(name) + r"\s*=\s*(0x[0-9A-Fa-f]+|\d+)", source) or \
        re.search(r"#define\s+" + re.escape(name) + r"\s+(0x[0-9A-Fa-f]+|\d+)", source)
    if not match:
        raise LookupError(f"constant not found in the sources: {name}")
    return int(match.group(1), 0)


def mutated(text, before, after):
    """The code under test with one deliberate fault, for a negative control; the original must contain it."""
    if text.count(before) != 1:
        raise LookupError(f"negative control no longer applies (found {text.count(before)} times): {before!r}")
    return text.replace(before, after, 1)


_directory = tempfile.TemporaryDirectory(prefix="halo-harness-")


@functools.lru_cache(maxsize=None)
def build(test, generated, cc="clang"):
    """Compile tests/<test>.c with the generated includes ((name, text), ...) for the 32-bit ABI; the executable."""
    work = Path(_directory.name) / f"{test}-{abs(hash(generated)):x}"
    work.mkdir(parents=True, exist_ok=True)
    for name, text in generated:
        (work / name).write_text(text)
    executable = work / test
    command = [cc, "-m32", "-std=gnu99", "-O2", "-Wall", "-Wno-unused-function", "-Wno-unused-variable",
               "-Wno-incompatible-pointer-types", "-Wno-multichar", "-Wno-parentheses",
               "-I", str(HARNESS / "include"), "-I", str(work), str(HARNESS / "tests" / f"{test}.c"), "-o", str(executable)]
    subprocess.run(command, check=True)
    return executable


def run(executable, case):
    """Run one case: (passed, output)."""
    result = subprocess.run([str(executable), case], capture_output=True, text=True)
    return result.returncode == 0, (result.stdout + result.stderr).strip()
