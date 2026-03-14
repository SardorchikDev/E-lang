"""
E Language — lexical environment (variable scoping).

Environments form a parent-chain.  Variable lookup walks up the chain;
variable assignment always writes to the innermost scope that already owns
the name, or creates it in the current scope if nobody owns it yet.
Function calls create a child of the closure environment.
"""
from __future__ import annotations
from .errors import ELangRuntimeError


class Environment:
    def __init__(self, parent: "Environment | None" = None):
        self._vars: dict[str, object] = {}
        self._parent = parent

    # ── read ──────────────────────────────────────────────────────────────────

    def get(self, name: str) -> object:
        if name in self._vars:
            return self._vars[name]
        if self._parent is not None:
            return self._parent.get(name)
        raise ELangRuntimeError(f"'{name}' is not defined")

    def has(self, name: str) -> bool:
        if name in self._vars:
            return True
        if self._parent is not None:
            return self._parent.has(name)
        return False

    # ── write ─────────────────────────────────────────────────────────────────

    def set(self, name: str, value: object) -> None:
        """Create or update a variable in the current (innermost) scope."""
        self._vars[name] = value

    def assign(self, name: str, value: object) -> None:
        """Update a variable in whichever scope owns it; create globally if new."""
        scope = self._owner(name)
        if scope is not None:
            scope._vars[name] = value
        else:
            # Create in the current (innermost) scope
            self._vars[name] = value

    def _owner(self, name: str) -> "Environment | None":
        if name in self._vars:
            return self
        if self._parent is not None:
            return self._parent._owner(name)
        return None

    # ── scope management ──────────────────────────────────────────────────────

    def child(self) -> "Environment":
        """Return a new child scope whose parent is this environment."""
        return Environment(parent=self)

    def __repr__(self) -> str:  # pragma: no cover
        names = list(self._vars.keys())
        return f"<Env {names} → {self._parent!r}>"
