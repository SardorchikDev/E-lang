from .lexer import Lexer
from .parser import Parser
from .interpreter import Interpreter
from .builtins import build_global_environment

global_env = build_global_environment()

def run(text):
    lexer = Lexer(text)
    tokens = lexer.make_tokens()
    
    parser = Parser(tokens)
    ast = parser.parse()
    
    interpreter = Interpreter(global_env)
    
    last_result = None
    for statement in ast:
        last_result = interpreter.visit(statement)
        
    return last_result
