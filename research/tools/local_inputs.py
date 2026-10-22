"""Locate the external, read-only game installation for local research tools."""

import os
from pathlib import Path


def game_directory() -> Path:
    """Resolve BULLET_WITCH_GAME_DIR, defaulting to ~/Games/BulletWitch-Steam.

    An unset or empty override uses the default. This only resolves a path;
    callers remain responsible for validating inputs and keeping them read-only.
    """
    configured = os.environ.get("BULLET_WITCH_GAME_DIR")
    path = Path(configured) if configured else Path.home() / "Games/BulletWitch-Steam"
    return path.expanduser().resolve()
