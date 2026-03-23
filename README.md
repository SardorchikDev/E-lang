# E-Lang

E-Lang stands for **English Language**. It is a small interpreted programming language written in C that tries to read like simple English while still being useful for real beginner-sized programs.

The implementation favors clarity over cleverness:

1. `lexer.c` turns source text into tokens.
2. `parser.c` turns tokens into an abstract syntax tree (AST).
3. `interpreter.c` walks the AST and runs the program.
4. `runtime.c` stores values, scopes, variables, records, lists, and functions.
5. `builtins.c` holds the standard built-in functions.
6. `main.c` exposes the CLI, REPL, formatter, linter, token dump, AST dump, and test runner.

E-Lang is line-based, forgiving where practical, and designed to be easy to extend later.

## Design

### What Makes E-Lang Feel Like English

- Most syntax is made of words instead of punctuation.
- Blocks use `end`.
- Arithmetic uses words like `plus`, `times`, and `mod`.
- Comparisons use phrases like `is greater than`.
- Lists start with `list of`.
- Records start with `record of`.

### Beginner-Friendly Choices

- Keywords are case-insensitive.
- Both full-line comments and inline comments are supported.
- Error messages show file, line, source text, and a pointer when possible.
- The interpreter prefers understandable runtime checks over speed.

### Scope Rules

- Top-level variables are global.
- `if`, `repeat`, `while`, and `for each` create block scopes.
- Functions create their own local scope.
- `let` creates a variable in the current scope.
- `set` updates an existing variable from the nearest matching scope.

## Project Files

- `main.c`: CLI, REPL, formatter, lint command, test command
- `lexer.c` / `lexer.h`: tokenization and source-line tracking
- `parser.c` / `parser.h`: AST and parser
- `interpreter.c` / `interpreter.h`: execution, imports, runtime errors, tracing
- `runtime.c` / `runtime.h`: values, lists, records, variables, functions, scopes
- `builtins.c` / `builtins.h`: standard library functions
- `files.c` / `files.h`: file loading and path resolution
- `dump.c` / `dump.h`: `--tokens` and `--ast`
- `formatter.c` / `formatter.h`: `--format`
- `analyzer.c` / `analyzer.h`: `--lint`
- `sample.elang`: main example program
- `sample_lib.elang`: imported helper used by the sample
- `tests/`: small regression suite
- `Makefile`: build helper

## Build

On Linux with `gcc`:

```bash
make
```

or:

```bash
gcc -std=c11 -Wall -Wextra -pedantic -g \
    -o e-lang \
    main.c lexer.c parser.c interpreter.c runtime.c builtins.c files.c dump.c formatter.c analyzer.c -lm
```

## Run

Run a program:

```bash
./e-lang sample.elang
```

Run with tracing:

```bash
./e-lang --trace sample.elang
```

Start the REPL:

```bash
./e-lang --repl
```

Dump tokens:

```bash
./e-lang --tokens sample.elang
```

Dump the AST:

```bash
./e-lang --ast sample.elang
```

Run the linter:

```bash
./e-lang --lint sample.elang
```

Format a file to standard output:

```bash
./e-lang --format sample.elang
```

Run tests:

```bash
./e-lang --test
```

## Full Syntax Overview

Keywords are case-insensitive, so `LET`, `Let`, and `let` all work.

### Comments

Full-line comment:

```text
note this is a comment
```

Inline comment:

```text
let x be 10 # this is also a comment
```

### Basic Values

Numbers:

```text
10
3.14
-2
```

Text:

```text
"Hello"
"Ada"
```

Booleans:

```text
true
false
```

Lists:

```text
list
list of 1, 2, 3
list of "red", "green", "blue"
```

Records:

```text
record
record of name is "Ada", age is 12
record of title is "Book", pages is 300
```

### Variables

Create:

```text
let age be 14
let name be "John"
let scores be list of 3, 4, 5
let person be record of name is "Ada", age is 12
```

Update:

```text
set age to age plus 1
set scores to call append with scores, 6
```

### Output

```text
say "Hello"
say name
say "Hello, " plus name
```

### Input

```text
ask "What is your name?" and store in name
ask "How old are you?" and store in age
```

If the user enters something numeric, E-Lang stores it as a number. If the user enters `true` or `false`, it stores a boolean. Otherwise it stores text.

### Imports

```text
use "library.elang"
```

`use` loads another E-Lang file once, using a path relative to the current file.

### Expressions

Arithmetic:

```text
5 plus 3
total minus 1
count times 2
amount divided by 4
value mod 3
2 power 5
```

Logic:

```text
true and false
not done
likes_math or likes_art
```

Comparisons:

```text
age is greater than 10
age is less than 18
age is equal to 14
name is not equal to ""
score is at least 60
score is at most 100
items contains 4
text contains "Ada"
person contains "name"
```

Grouping:

```text
(score plus bonus) times 2
not (age is less than 13)
```

Function calls inside expressions:

```text
call length with name
call append with numbers, 4
call greet with "Ada"
```

Important note: when a function call is only part of a larger expression, grouping is the clearest form:

```text
if (call get_field with user, "age") is greater than 10 then
    say "older than ten"
end
```

### Conditionals

```text
if age is greater than 10 then
    say "Older than ten"
end
```

With `else`:

```text
if age is at least 18 then
    say "Adult"
else
    say "Not an adult yet"
end
```

With `else if`:

```text
if score is at least 90 then
    say "A"
else if score is at least 80 then
    say "B"
else
    say "Keep practicing"
end
```

### Loops

Repeat:

```text
repeat 5 times
    say "Hi"
end
```

While:

```text
while counter is less than 10 do
    say counter
    set counter to counter plus 1
end
```

For each:

```text
for each score in scores
    say score
end
```

`for each` works with lists, text, and records. When used on a record, it loops over the field names.

Loop control:

```text
break
continue
```

### Functions

Define:

```text
define function greet
    say "Hello"
end
```

With parameters:

```text
define function add with left, right
    return left plus right
end
```

Call:

```text
call greet
let total be call add with 4, 5
say call add with 4, 5
```

Return:

```text
define function square with n
    return n times n
end
```

## Built-In Functions

Assertions:

- `assert`
- `assert_equal`

Lists:

- `length`
- `item`
- `append`
- `set_item`
- `insert_item`
- `remove_item`
- `slice`
- `sort`

Conversions and type info:

- `to_number`
- `to_text`
- `type_of`

Text:

- `lowercase`
- `uppercase`
- `trim`
- `split`
- `join`

Math:

- `sqrt`
- `random`

Files:

- `read_file`
- `write_file`
- `append_file`
- `file_exists`

Records:

- `get_field`
- `set_field`
- `has_field`
- `keys`

## Practical Grammar

This is the implemented shape of the language:

```text
program             -> statement*

statement           -> use_statement
                    | let_statement
                    | set_statement
                    | say_statement
                    | ask_statement
                    | if_statement
                    | repeat_statement
                    | while_statement
                    | for_each_statement
                    | function_statement
                    | call_statement
                    | return_statement
                    | break_statement
                    | continue_statement
                    | note_statement

use_statement       -> "use" STRING
let_statement       -> "let" NAME "be" expression
set_statement       -> "set" NAME "to" expression
say_statement       -> "say" expression
ask_statement       -> "ask" expression "and" "store" "in" NAME

if_statement        -> "if" expression ["then"] block
                       ("else" block | "else" "if" expression ["then"] block)?
                       "end"

repeat_statement    -> "repeat" expression ("time" | "times") block "end"
while_statement     -> "while" expression ["do"] block "end"
for_each_statement  -> "for" "each" NAME "in" expression block "end"

function_statement  -> "define" "function" NAME ["with" param_list] block "end"
call_statement      -> call_expression
return_statement    -> "return" ["with"] expression?
break_statement     -> "break"
continue_statement  -> "continue"
note_statement      -> "note" ...

param_list          -> NAME ("," NAME)*
block               -> statement*

expression          -> logic_or
logic_or            -> logic_and ("or" logic_and)*
logic_and           -> comparison ("and" comparison)*
comparison          -> additive
                       ( "contains" additive
                       | "is" "greater" "than" additive
                       | "is" "less" "than" additive
                       | "is" "equal" "to" additive
                       | "is" "not" "equal" "to" additive
                       | "is" "at" "least" additive
                       | "is" "at" "most" additive )*
additive            -> multiplicative (("plus" | "minus") multiplicative)*
multiplicative      -> power (("times" | "divided" ["by"] | "mod") power)*
power               -> unary ("power" power)?
unary               -> ("minus" | "not") unary | primary
primary             -> NUMBER
                    | STRING
                    | "true"
                    | "false"
                    | NAME
                    | "(" expression ")"
                    | call_expression
                    | list_expression
                    | record_expression

call_expression     -> "call" NAME ["with" expression ("," expression)*]
list_expression     -> "list" ["of" expression ("," expression)*]
record_expression   -> "record" ["of" field ("," field)*]
field               -> (NAME | STRING) "is" expression
```

## Sample Walkthrough

`sample.elang` does the following:

1. Imports `sample_lib.elang`.
2. Builds a `student` record with a name, a level, and a score list.
3. Calls imported functions to greet the user and describe the student level.
4. Uses `for each` to print every score.
5. Sums the scores with `set`.
6. Uses `contains` and `split` / `join`.
7. Reads input with `ask`.

Run it with:

```bash
./e-lang sample.elang
```

## Future Upgrades

The current project is already a real mini-language, but obvious next steps are:

- first-class modules and exports instead of simple `use`
- dictionaries with richer field access syntax like `person.name`
- a stronger standard library
- better argument parsing for complex nested call expressions
- a bytecode backend for speed
- richer static analysis
- source maps for formatter/linter fixes
- package management and a standard module folder
# E-lang
# E-lang
