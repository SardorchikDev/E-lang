"""
E Language — token types and Token dataclass.
"""
from enum import Enum
from dataclasses import dataclass
from typing import Any


class TT(Enum):
    # ── Literal value types ───────────────────────────────────────────────────
    NUMBER   = "NUMBER"
    TEXT     = "TEXT"
    IDENT    = "IDENT"

    # ── Core statements ───────────────────────────────────────────────────────
    SET       = "set"
    TO        = "to"
    SAY       = "say"
    ASK       = "ask"
    AND       = "and"
    STORE     = "store"
    IN        = "in"

    # ── Control flow ──────────────────────────────────────────────────────────
    IF         = "if"
    THEN       = "then"
    OTHERWISE  = "otherwise"
    END        = "end"
    WHILE      = "while"
    DO         = "do"
    REPEAT     = "repeat"
    TIMES      = "times"
    FOR        = "for"
    EACH       = "each"
    STOP       = "stop"
    SKIP       = "skip"

    # ── Functions ─────────────────────────────────────────────────────────────
    DEFINE    = "define"
    WITH      = "with"
    CALL      = "call"
    GIVE      = "give"
    BACK      = "back"
    RETURN    = "return"

    # ── List mutation ─────────────────────────────────────────────────────────
    ADD       = "add"
    REMOVE    = "remove"
    FROM      = "from"

    # ── Arithmetic ────────────────────────────────────────────────────────────
    PLUS       = "plus"
    MINUS      = "minus"
    MULTIPLIED = "multiplied"
    DIVIDED    = "divided"
    BY         = "by"
    MOD        = "mod"

    # ── Comparison / logic ────────────────────────────────────────────────────
    IS        = "is"
    NOT       = "not"
    EQUALS    = "equals"
    EQUAL     = "equal"
    GREATER   = "greater"
    LESS      = "less"
    THAN      = "than"
    OR        = "or"
    DOES      = "does"

    # ── List / string ops ─────────────────────────────────────────────────────
    A          = "a"
    LIST       = "list"
    FIRST      = "first"
    LAST       = "last"
    OF         = "of"
    AT         = "at"
    POSITION   = "position"
    JOINED     = "joined"
    LENGTH     = "length"
    CONTAINS   = "contains"
    UPPERCASE  = "uppercase"
    LOWERCASE  = "lowercase"
    REVERSED   = "reversed"

    # ── Boolean / null literals ───────────────────────────────────────────────
    YES       = "yes"
    NO        = "no"
    TRUE      = "true"
    FALSE     = "false"
    NOTHING   = "nothing"

    # ── Punctuation ───────────────────────────────────────────────────────────
    LBRACKET  = "["
    RBRACKET  = "]"
    LPAREN    = "("
    RPAREN    = ")"
    COMMA     = ","

    # ── Meta ──────────────────────────────────────────────────────────────────
    NEWLINE   = "NEWLINE"
    EOF       = "EOF"


@dataclass
class Token:
    type: TT
    value: Any
    line: int

    def __repr__(self) -> str:
        return f"Token({self.type.name}, {self.value!r}, L{self.line})"
