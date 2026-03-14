from .tokens import TokenType
from .ast import *
from .errors import ParserError

class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.tok_idx = -1
        self.current_tok = None
        self.advance()

    def advance(self):
        self.tok_idx += 1
        self.current_tok = self.tokens[self.tok_idx] if self.tok_idx < len(self.tokens) else None

    def parse(self):
        statements =[]
        while self.current_tok.type != TokenType.EOF:
            statements.append(self.expr())
        return statements

    def expr(self):
        # Handle Variable Assignment (e.g., x = 5)
        if self.current_tok.type == TokenType.IDENTIFIER:
            next_tok = self.tokens[self.tok_idx + 1] if self.tok_idx + 1 < len(self.tokens) else None
            if next_tok and next_tok.type == TokenType.EQUALS:
                var_name = self.current_tok
                self.advance() # Skip identifier
                self.advance() # Skip equals
                value = self.expr()
                return VarAssignNode(var_name, value)
        
        # Handle Math Operations
        return self.bin_op(self.term, (TokenType.PLUS, TokenType.MINUS))

    def term(self):
        return self.bin_op(self.factor, (TokenType.MUL, TokenType.DIV))

    def factor(self):
        tok = self.current_tok

        if tok.type in (TokenType.PLUS, TokenType.MINUS):
            self.advance()
            return BinOpNode(NumberNode(Token(TokenType.NUMBER, 0)), tok, self.factor())
        
        elif tok.type in (TokenType.NUMBER, TokenType.STRING):
            self.advance()
            node = NumberNode(tok) if tok.type == TokenType.NUMBER else StringNode(tok)
            return node
            
        elif tok.type == TokenType.IDENTIFIER:
            self.advance()
            if self.current_tok.type == TokenType.LPAREN: # Function Call
                self.advance()
                args =[]
                if self.current_tok.type != TokenType.RPAREN:
                    args.append(self.expr())
                if self.current_tok.type != TokenType.RPAREN:
                    raise ParserError("Expected ')'")
                self.advance()
                return CallNode(VarAccessNode(tok), args)
            return VarAccessNode(tok)

        elif tok.type == TokenType.LPAREN:
            self.advance()
            expr = self.expr()
            if self.current_tok.type != TokenType.RPAREN: raise ParserError("Expected ')'")
            self.advance()
            return expr

        raise ParserError(f"Unexpected token {tok}")

    def bin_op(self, func, ops):
        left = func()
        while self.current_tok.type in ops:
            op_tok = self.current_tok
            self.advance()
            right = func()
            left = BinOpNode(left, op_tok, right)
        return left
