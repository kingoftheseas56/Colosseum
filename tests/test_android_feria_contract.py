"""Feria is permanently desktop-only, including shared SessionStore dependencies."""
import re
from test_android_vault_contract import _active_source, _application_sources


def test_android_has_no_feria_implementation():
    for path in _application_sources(True):
        assert not re.search(r"(?:^|/)feria/", path.replace("\\", "/")), path
    for relative in ("main.cpp", "SessionStore.h"):
        active = _active_source(relative, True)
        assert not re.search(r"\b(?:FeriaBrowser\w*|FeriaAccountStore|PorticoComposition)\b", active), relative
    desktop = _active_source("SessionStore.h", False)
    assert "FeriaBrowserOperations" in desktop


if __name__ == "__main__":
    test_android_has_no_feria_implementation()
    print("ANDROID_FERIA_EXCLUSION_OK")
