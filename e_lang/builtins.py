import os
from .environment import Environment

class BuiltInFunction:
    def __init__(self, name, py_func):
        self.name = name
        self.py_func = py_func
    
    def execute(self, args):
        return self.py_func(*args)

# Create a Python function to clear the terminal
def clear_console():
    os.system('cls' if os.name == 'nt' else 'clear')
    return None

def build_global_environment():
    env = Environment()
    
    # Adding the 'say' function (E's version of print)
    env.set("say", BuiltInFunction("say", print))
    
    # Inject the clear function into E!
    env.set("clear", BuiltInFunction("clear", clear_console))
    
    return env
