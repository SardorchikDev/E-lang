import sys
from .repl import start_repl
from . import run
from .errors import EError

def main():
    if len(sys.argv) == 1:
        start_repl()
    elif len(sys.argv) == 2:
        filename = sys.argv[1]
        try:
            with open(filename, 'r') as file:
                code = file.read()
            run(code)
        except FileNotFoundError:
            print(f"Error: File '{filename}' not found.")
        except EError as e:
            print(e)
    else:
        print("Usage: python -m e_lang.cli [filename.e]")

if __name__ == '__main__':
    main()
