import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).parents[1]


def test_event_deduplicator(tmp_path: Path) -> None:
    compiler = shutil.which("c++")
    assert compiler is not None, "a C++ compiler is required for the deduplication test"
    binary = tmp_path / "event_deduplicator_test"
    subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT}",
            str(ROOT / "components/honeywell_activlink/event_deduplicator.cpp"),
            str(ROOT / "tests/event_deduplicator_test.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([binary], check=True)
