from . import run
from .errors import EError

def start_repl():
    print("Welcome to E v1.0! Type 'exit' to quit.")
    while True:
        try:
            text = input("E > ")
            if text.strip() == "": continue
            if text.strip().lower() == "exit": break
            
            result = run(text)
            # Only print the result if it's not None (like math)
            if result is not None:
                print(result)
        except EError as e:
            print(e)
        except Exception as e:
            print(f"System Error: {e}")
