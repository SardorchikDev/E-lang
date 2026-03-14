"""
E Language — custom exception hierarchy and control-flow signals.
"""


class ELangError(Exception):
    """Base class for all E language errors."""


class LexError(ELangError):
    """Raised by the lexer on invalid input."""


class ParseError(ELangError):
    """Raised by the parser on syntactically invalid source."""


class ELangRuntimeError(ELangError):
    """Raised by the interpreter at runtime."""


# ── Internal control-flow signals (not errors) ────────────────────────────────

class ReturnSignal(BaseException):
    """Propagates a return value up the call stack."""
    def __init__(self, value):
        self.value = value


class BreakSignal(BaseException):
    """Propagates a 'stop' (break) out of a loop."""


class ContinueSignal(BaseException):
    """Propagates a 'skip' (continue) to the next loop iteration."""
