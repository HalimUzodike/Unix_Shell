# smallsh - A Simple Unix Shell Implementation

`smallsh` is a lightweight Unix shell implementation written in C. It provides basic shell functionality with support for built-in commands, input/output redirection, background processes, and signal handling.

## Features

- Command execution with support for built-in commands (`cd`, `exit`)
- Input/output redirection (`<`, `>`, `>>`)
- Background process execution (`&`)
- Environment variable expansion (`$VAR`, `${VAR}`)
- Special parameter expansion (`$$`, `$?`, `$!`)
- Signal handling (SIGINT, SIGTSTP)
- Comment support (`#`)

## Built-in Commands

- `cd [directory]`: Change the current working directory
- `exit [status]`: Exit the shell with an optional status code

## Usage

Compile the program:

`gcc -o smallsh smallsh.c`

Run the shell:

`./smallsh`

## Implementation Details

- Uses POSIX-compliant system calls and functions
- Implements a custom word splitting function
- Handles parameter expansion with a simple string builder
- Manages background processes and reports their status

## Limitations

- Maximum of 512 words per command
- Does not support advanced features like piping or command substitution

This project is ideal for learning about Unix system programming, process management, and basic shell implementation techniques.
