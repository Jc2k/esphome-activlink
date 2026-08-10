"""Build-time ESPHome API overlay for runtime-visible sub-devices.

ESPHome 2026.7.3 represents ``DeviceInfoResponse.devices`` as a fixed-size
``std::array``. That is appropriate for YAML-only sub-devices, but it forces
all eight ActivLink capacity slots into discovery even when most are empty.

This overlay keeps the upstream API component and changes only the generated
container and its population call to a bounded ``StaticVector``. The exact
source matches are deliberate: an ESPHome upgrade fails closed until this
small compatibility patch is reviewed against the new upstream source.
"""

from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Iterator

from esphome import loader
from esphome.core import EsphomeError


_PATCHES = {
    "api_pb2.h": (
        (
            "  std::array<DeviceInfo, ESPHOME_DEVICE_COUNT> devices{};",
            "  StaticVector<DeviceInfo, ESPHOME_DEVICE_COUNT> devices{};",
        ),
    ),
    "api_connection.cpp": (
        (
            "    auto &device_info = resp.devices[device_index++];",
            "    auto &device_info = resp.devices.emplace_next();\n"
            "    device_index++;",
        ),
    ),
}


def patch_api_resource(resource: str, source: str) -> str:
    """Apply and validate the overlay for one upstream API source file."""
    replacements = _PATCHES.get(resource)
    if replacements is None:
        return source

    patched = source
    for expected, replacement in replacements:
        matches = patched.count(expected)
        if matches != 1:
            raise EsphomeError(
                f"ESPHome API compatibility check failed for {resource}: "
                f"expected one source match, found {matches}"
            )
        patched = patched.replace(expected, replacement, 1)
    return patched


@dataclass(frozen=True)
class _PatchedResource:
    original: loader.FileResource

    @property
    def package(self) -> str:
        return self.original.package

    @property
    def resource(self) -> str:
        return self.original.resource

    @contextmanager
    def path(self) -> Iterator[Path]:
        with self.original.path() as original_path:
            source = original_path.read_text()
            patched = patch_api_resource(self.resource, source)
            with TemporaryDirectory(prefix="activlink-api-") as temporary:
                path = Path(temporary) / self.resource
                path.write_text(patched)
                yield path


class _PatchedApiManifest:
    def __init__(self, original: loader.ComponentManifest) -> None:
        self._original = original

    def __getattr__(self, name):
        return getattr(self._original, name)

    @property
    def resources(self):
        return [
            _PatchedResource(resource)
            if resource.resource in _PATCHES
            else resource
            for resource in self._original.resources
        ]


def install_dynamic_device_info_patch() -> None:
    """Overlay the two API resources before ESPHome copies its source tree."""
    manifest = loader.get_component("api", exception=True)
    if isinstance(manifest, _PatchedApiManifest):
        return
    loader._replace_component_manifest("api", _PatchedApiManifest(manifest))
