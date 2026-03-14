"""
E Language — lexer.

Converts raw source text into a flat list of Token objects.
Words are matched case-insensitively against the keyword table; anything
not in the table becomes an IDENT token.  Identifiers preserve original
casing so variable names are case-sensitive while keywords are not.
"""
from .tokens import Token, TT
from .errors import LexError


# Every English keyword maps to its token type.
# Aliases (else, break, continue, …) map to the canonical type.
KEYWORDS: dict[str, TT] = {
    # statements
    "set": TT.SET, "to": TT.TO, "say": TT.SAY, "ask": TT.ASK,
    "and": TT.AND, "store": TT.STORE, "in": TT.IN,
    # control flow
    "if": TT.IF, "then": TT.THEN,
    "otherwise": TT.OTHERWISE, "else": TT.OTHERWISE,
    "end": TT.END,
    "while": TT.WHILE, "do": TT.DO,
    "repeat": TT.REPEAT, "times": TT.TIMES,
    "for": TT.FOR, "each": TT.EACH,
    "stop": TT.STOP, "break": TT.STOP,
    "skip": TT.SKIP, "continue": TT.SKIP,
    # functions
    "define": TT.DEFINE, "with": TT.WITH,
    "call": TT.CALL,
    "give": TT.GIVE, "back": TT.BACK,
    "return": TT.RETURN,
    # list mutation
    "add": TT.ADD, "remove": TT.REMOVE, "from": TT.FROM,
    # arithmetic
    "plus": TT.PLUS, "minus": TT.MINUS,
    "multiplied": TT.MULTIPLIED,
    "divided": TT.DIVIDED, "by": TT.BY,
    "mod": TT.MOD, "modulo": TT.MOD,
    # comparison / logic
    "is": TT.IS, "not": TT.NOT,
    "equals": TT.EQUALS, "equal": TT.EQUAL,
    "greater": TT.GREATER, "less": TT.LESS, "than": TT.THAN,
    "or": TT.OR, "does": TT.DOES,
    # list / string ops
    "a": TT.A, "an": TT.A,
    "list": TT.LIST,
    "first": TT.FIRST, "last": TT.LAST, "of": TT.OF,
    "at": TT.AT, "position": TT.POSITION,
    "joined": TT.JOINED, "length": TT.LENGTH,
    "contains": TT.CONTAINS,
    "uppercase": TT.UPPERCASE, "lowercase": TT.LOWERCASE,
    "reversed": TT.REVERSED,
    # boolean / null
    "yes": TT.YES, "no": TT.NO,
    "true": TT.TRUE, "false": TT.FALSE,
    "nothing": TT.NOTHING, "null": TT.NOTHING, "none": TT.NOTHING,
}

# Token types after which a leading '-' starts a negative number literal
_NEGATIVE_OK = frozenset({
    None,
    TT.TO, TT.COMMA, TT.LBRACKET, TT.LPAREN,
    TT.PLUS, TT.MINUS, TT.MULTIPLIED, TT.DIVIDED,
    TT.BY, TT.MOD, TT.AND, TT.OR, TT.NEWLINE,
    TT.THEN, TT.DO,
})


class Lexer:
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1

    # ── helpers ───────────────────────────────────────────────────────────────

    def _err(self, msg: str) -> None:
        raise LexError(f"Line {self.line}: {msg}")

    def _eof(self) -> bool:
        return self.pos >= len(self.source)

    def _cur(self) -> str | None:
        return self.source[self.pos] if not self._eof() else None

    def _peek(self, n: int = 1) -> str | None:
        p = self.pos + n
        return self.source[p] if p < len(self.source) else None

    def _adv(self) -> str:
        ch = self.source[self.pos]
        self.pos += 1
        if ch == "\n":
            self.line += 1
        return ch

    # ── main pass ─────────────────────────────────────────────────────────────

    def tokenize(self) -> list[Token]:
        tokens: list[Token] = []

        while not self._eof():
            # Skip horizontal whitespace
            while not self._eof() and self._cur() in " \t\r":
                self._adv()
            if self._eof():
                break

            ch   = self._cur()
            line = self.line
            prev = tokens[-1].type if tokens else None

            # ── comments ──────────────────────────────────────────────────────
            if ch == "/" and self._peek() == "/":
                while not self._eof() and self._cur() != "\n":
                    self._adv()
                continue
            if ch == "#":
                while not self._eof() and self._cur() != "\n":
                    self._adv()
                continue

            # ── newlines ──────────────────────────────────────────────────────
            if ch == "\n":
                self._adv()
                if prev != TT.NEWLINE:
                    tokens.append(Token(TT.NEWLINE, "\n", line))
                continue

            # ── string literals ───────────────────────────────────────────────
            if ch in ('"', "'"):
                tokens.append(self._scan_string(ch))
                continue

            # ── numeric literals ──────────────────────────────────────────────
            if ch.isdigit():
                tokens.append(self._scan_number())
                continue

            if ch == "-" and self._peek() and self._peek().isdigit():
                if prev in _NEGATIVE_OK:
                    tokens.append(self._scan_number())
                    continue

            # ── punctuation ───────────────────────────────────────────────────
            _punct = {
                "[": TT.LBRACKET, "]": TT.RBRACKET,
                "(": TT.LPAREN,   ")": TT.RPAREN,
                ",": TT.COMMA,
            }
            if ch in _punct:
                self._adv()
                tokens.append(Token(_punct[ch], ch, line))
                continue

            # ── words (keywords + identifiers) ────────────────────────────────
            if ch.isalpha() or ch == "_":
                word = self._scan_word()
                tt   = KEYWORDS.get(word.lower(), TT.IDENT)
                # Identifiers keep original casing; keywords are lowercased
                val  = word if tt == TT.IDENT else word.lower()
                tokens.append(Token(tt, val, line))
                continue

            self._err(f"Unexpected character: {ch!r}")

        tokens.append(Token(TT.EOF, None, self.line))
        return tokens

    # ── sub-scanners ──────────────────────────────────────────────────────────

    def _scan_string(self, quote: str) -> Token:
        line = self.line
        self._adv()                  # opening quote
        buf: list[str] = []
        escapes = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'"}
        while not self._eof() and self._cur() != quote:
            ch = self._cur()
            if ch == "\\":
                self._adv()
                buf.append(escapes.get(self._adv(), ""))
            else:
                buf.append(self._adv())
        if self._eof():
            raise LexError(f"Line {line}: Unterminated string literal")
        self._adv()                  # closing quote
        return Token(TT.TEXT, "".join(buf), line)

    def _scan_number(self) -> Token:
        line  = self.line
        start = self.pos
        if self._cur() == "-":
            self._adv()
        while not self._eof() and self._cur().isdigit():
            self._adv()
        if (not self._eof() and self._cur() == "."
                and self._peek() and self._peek().isdigit()):
            self._adv()
            while not self._eof() and self._cur().isdigit():
                self._adv()
        raw = self.source[start:self.pos]
        val = float(raw) if "." in raw else int(raw)
        return Token(TT.NUMBER, val, line)

    def _scan_word(self) -> str:
        start = self.pos
        while not self._eof() and (self._cur().isalnum() or self._cur() == "_"):
            self._adv()
        return self.source[start:self.pos]
