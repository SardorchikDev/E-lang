"""
E Language — Abstract Syntax Tree node definitions.

Every node is a frozen-ish dataclass with an optional `line` field for
error reporting.  Statements produce effects; expressions produce values.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Any


@dataclass
class Node:
    line: int = field(default=0, repr=False, compare=False)


# ══ Statements ════════════════════════════════════════════════════════════════

@dataclass
class Program(Node):
    body: list[Node] = field(default_factory=list)


@dataclass
class SetStmt(Node):
    """set <name> to <value>"""
    name: str  = ""
    value: Node = None   # type: ignore[assignment]


@dataclass
class SayStmt(Node):
    """say <value>"""
    value: Node = None   # type: ignore[assignment]


@dataclass
class AskStmt(Node):
    """ask <prompt> and store in <name>"""
    prompt: Node = None  # type: ignore[assignment]
    name: str = ""


@dataclass
class IfStmt(Node):
    """if <cond> then … [otherwise …] end"""
    condition: Node         = None   # type: ignore[assignment]
    then_body: list[Node]   = field(default_factory=list)
    else_body: list[Node]   = field(default_factory=list)


@dataclass
class WhileStmt(Node):
    """while <cond> do … end"""
    condition: Node       = None   # type: ignore[assignment]
    body: list[Node]      = field(default_factory=list)


@dataclass
class RepeatStmt(Node):
    """repeat <count> times … end  — exposes 'current' (1-indexed)"""
    count: Node           = None   # type: ignore[assignment]
    body: list[Node]      = field(default_factory=list)


@dataclass
class ForEachStmt(Node):
    """for each <item> in <list> do … end"""
    item_name: str        = ""
    list_expr: Node       = None   # type: ignore[assignment]
    body: list[Node]      = field(default_factory=list)


@dataclass
class DefineStmt(Node):
    """define <name> [with <p1> and <p2> …] … end"""
    name: str             = ""
    params: list[str]     = field(default_factory=list)
    body: list[Node]      = field(default_factory=list)


@dataclass
class ReturnStmt(Node):
    """give back <value>  /  return <value>"""
    value: Node = None   # type: ignore[assignment]


@dataclass
class AddToStmt(Node):
    """add <value> to <list>"""
    value: Node = None   # type: ignore[assignment]
    target: str = ""


@dataclass
class RemoveFromStmt(Node):
    """remove <value> from <list>"""
    value: Node = None   # type: ignore[assignment]
    target: str = ""


@dataclass
class StopStmt(Node):
    """stop  (break out of loop)"""


@dataclass
class SkipStmt(Node):
    """skip  (continue to next iteration)"""


@dataclass
class ExprStmt(Node):
    """A bare expression used as a statement (e.g. a call whose value is discarded)."""
    expr: Node = None    # type: ignore[assignment]


# ══ Expressions ═══════════════════════════════════════════════════════════════

@dataclass
class NumberLit(Node):
    value: int | float = 0


@dataclass
class TextLit(Node):
    value: str = ""


@dataclass
class BoolLit(Node):
    value: bool = True


@dataclass
class NothingLit(Node):
    pass


@dataclass
class ListLit(Node):
    elements: list[Node] = field(default_factory=list)


@dataclass
class Identifier(Node):
    name: str = ""


@dataclass
class BinaryOp(Node):
    """General binary operation node.

    op values: plus minus times divided mod
               eq ne gt lt ge le
               and or
               type_is
    """
    left:  Node = None   # type: ignore[assignment]
    op:    str  = ""
    right: Node = None   # type: ignore[assignment]


@dataclass
class UnaryOp(Node):
    """op values: not  neg"""
    op:      str  = ""
    operand: Node = None  # type: ignore[assignment]


@dataclass
class CallExpr(Node):
    """call <name> [with <arg1> and <arg2> …]"""
    name: str          = ""
    args: list[Node]   = field(default_factory=list)


@dataclass
class JoinedWith(Node):
    """<left> joined with <right>  — string / list concatenation"""
    left:  Node = None  # type: ignore[assignment]
    right: Node = None  # type: ignore[assignment]


@dataclass
class LengthOf(Node):
    value: Node = None  # type: ignore[assignment]


@dataclass
class FirstOf(Node):
    value: Node = None  # type: ignore[assignment]


@dataclass
class LastOf(Node):
    value: Node = None  # type: ignore[assignment]


@dataclass
class UppercaseOf(Node):
    value: Node = None  # type: ignore[assignment]


@dataclass
class LowercaseOf(Node):
    value: Node = None  # type: ignore[assignment]


@dataclass
class ReversedOf(Node):
    value: Node = None  # type: ignore[assignment]


@dataclass
class ItemAt(Node):
    """<collection> at position <index>"""
    collection: Node = None  # type: ignore[assignment]
    index:      Node = None  # type: ignore[assignment]


@dataclass
class ContainsCheck(Node):
    """<collection> contains <item>"""
    collection: Node = None  # type: ignore[assignment]
    item:       Node = None  # type: ignore[assignment]
