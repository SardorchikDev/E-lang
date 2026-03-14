from .ast import *
from .tokens import TokenType  # <-- THIS WAS MISSING!
from .errors import ERuntimeError
from .builtins import BuiltInFunction

class Interpreter:
    def __init__(self, env):
        self.env = env

    def visit(self, node):
        method_name = f'visit_{type(node).__name__}'
        method = getattr(self, method_name, self.no_visit_method)
        return method(node)

    def no_visit_method(self, node):
        raise Exception(f"No visit_{type(node).__name__} method defined")

    def visit_NumberNode(self, node): return node.tok.value
    def visit_StringNode(self, node): return node.tok.value

    def visit_VarAccessNode(self, node):
        var_name = node.var_name_tok.value
        val = self.env.get(var_name)
        if val is None: raise ERuntimeError(f"'{var_name}' is not defined")
        return val

    def visit_VarAssignNode(self, node):
        var_name = node.var_name_tok.value
        value = self.visit(node.value_node)
        self.env.set(var_name, value)
        return value

    def visit_BinOpNode(self, node):
        left = self.visit(node.left_node)
        right = self.visit(node.right_node)

        if node.op_tok.type == TokenType.PLUS: return left + right
        elif node.op_tok.type == TokenType.MINUS: return left - right
        elif node.op_tok.type == TokenType.MUL: return left * right
        elif node.op_tok.type == TokenType.DIV:
            if right == 0: raise ERuntimeError("Division by zero")
            return left / right

    def visit_CallNode(self, node):
        func = self.visit(node.node_to_call)
        args = [self.visit(arg) for arg in node.arg_nodes]
        if isinstance(func, BuiltInFunction):
            return func.execute(args)
        raise ERuntimeError("Identifier is not a function")
