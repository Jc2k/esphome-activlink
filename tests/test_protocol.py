import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).parents[1]


def test_protocol_decoder(tmp_path: Path) -> None:
    compiler = shutil.which("c++")
    assert compiler is not None, "a C++ compiler is required for the decoder test"
    binary = tmp_path / "protocol_test"
    subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT}",
            str(ROOT / "components/honeywell_activlink/protocol.cpp"),
            str(ROOT / "tests/protocol_test.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([binary], check=True)

