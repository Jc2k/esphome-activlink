from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from prepare_release import validate_release_version  # noqa: E402


@pytest.mark.parametrize("version", ["1.1.0", "1.1.1", "2.0.0"])
def test_deployable_release_versions_are_accepted(version: str) -> None:
    validate_release_version(version)


@pytest.mark.parametrize("version", ["0.0.1", "1.0.0", "1.0.99"])
def test_pre_fsk_release_versions_are_rejected(version: str) -> None:
    with pytest.raises(ValueError, match="below the deployable minimum 1.1.0"):
        validate_release_version(version)


@pytest.mark.parametrize("version", ["v1.1.0", "1.1", "1.1.0-rc.1"])
def test_non_numeric_release_versions_are_rejected(version: str) -> None:
    with pytest.raises(ValueError, match="invalid semantic version"):
        validate_release_version(version)
