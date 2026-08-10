import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).parents[1]


def test_runtime_pairing_table(tmp_path: Path) -> None:
    compiler = shutil.which("c++")
    assert compiler is not None, "a C++ compiler is required for the pairing test"
    binary = tmp_path / "pairing_test"
    subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT}",
            str(ROOT / "components/honeywell_activlink/pairing.cpp"),
            str(ROOT / "tests/pairing_test.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([binary], check=True)
