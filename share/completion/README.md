return # ignore this line, just in case the file is sourced by the shell

------------------------------------------------------------------------------

# How to Write Completion Scripts for yash

This document explains how to write command-line completion scripts for the yash shell.

Completion scripts are shell scripts that define how to complete arguments for a specific command. They are located in the `share/completion` directory.

## File Naming and Basic Structure

A completion script for a command, for example, `mycommand`, should be placed in a file named `share/completion/mycommand`.

The script must define a function named `completion/mycommand`. This function is automatically called when the user tries to complete an argument for `mycommand`.

Here is a simple example for a command `alias` that has a few options and then completes existing alias names:

```sh
# share/completion/alias
function completion/alias {
    # Define available options for the command
    typeset OPTIONS ARGOPT PREFIX
    OPTIONS=(
        "g --global; define global aliases"
        "p --prefix; print aliases as reusable shell commands"
        "--help"
    )

    # Parse the command line
    command -f completion//parseoptions -es

    # Decide what to complete based on the parse result
    case $ARGOPT in
    (-)
        # The user is completing an option (e.g., typing `alias --g...`)
        command -f completion//completeoptions
        ;;
    (*)
        # The user is completing an operand (an argument)
        complete -a # Use the built-in `complete` to suggest alias names
        ;;
    esac
}
```

## Core Concepts and Variables

The completion system uses a few variables to manage the state:

- `$WORDS`: An array containing the words on the command line being edited. `${WORDS[1]}` is the command name itself.
- `$TARGETWORD`: The word the cursor is on, which needs to be completed.
- `$OPTIONS`: An array you define in your script that lists the command's valid options.
- `$ARGOPT`: Set by `completion//parseoptions`. It indicates what kind of argument is being completed (an option, an operand, or an argument to an option).
- `$PREFIX`: Also set by `completion//parseoptions`. It holds the prefix of a word being completed, which is useful for complex completions (e.g., `foo=bar`, where `foo=` would be the prefix).

## Utility Functions

The `share/completion/INIT` file provides several helper functions to simplify writing completion scripts. These are automatically loaded. See the comments in that file for full details.

### `completion//parseoptions`

This is the most important helper function. It parses the command line (`$WORDS` and `$TARGETWORD`) according to the option specifications you provide in the `$OPTIONS` array.

Usage: `command -f completion//parseoptions [options]`

- Input: It reads the `$WORDS`, `$TARGETWORD`, and `$OPTIONS` variables.
- Output: It sets the `$ARGOPT` and `$PREFIX` variables and can modify `$WORDS`.

**The `$OPTIONS` Array**

Each element in this array is a string that defines one or more options. The format is:

`"option-list[;description]"`

- **`option-list`**: A space-separated list of option specifications.
    - Short options: A single character, e.g., `p`.
    - Long options: The full option name, e.g., `--print`.
- Each option specification can have an optional suffix to indicate argument requirements:
    - No argument: `p`, `--help`
    - Mandatory argument: `f:`, `--file:`
    - Optional argument: `o::`, `--output::`
- **`description`**: An optional description of the options, separated by a semicolon.

Example `$OPTIONS` array:

```sh
OPTIONS=(
    "p --print; print aliases as reusable shell commands"
    "f: --file:; read from the specified file"
    "o:: --output::; optionally specify an output file"
)
```

**`$ARGOPT` Values**

After parsing, `$ARGOPT` will contain one of:

- `-`: The user is completing an option (e.g., `cmd -...` or `cmd --...`).
- `option-name`: The user is completing an argument for `option-name` (e.g., `cmd -f ...` or `cmd --file=...`).
- An empty string: The user is completing a regular operand (not an option or its argument).

### `completion//completeoptions`

This function generates completion candidates for command-line options based on the `$OPTIONS` array. It should typically be called when `completion//parseoptions` sets `$ARGOPT` to `-`.

Usage: `command -f completion//completeoptions`

It automatically reads the `$OPTIONS` array and the current `$TARGETWORD` to suggest matching long and short options, including their descriptions.

### `completion//getoperands`

This function is a simple utility to remove all options from the `$WORDS` array, leaving only the operands. This is useful after calling `completion//parseoptions` to easily work with the non-option arguments.

Usage: `command -f completion//getoperands`

### `completion//reexecute`

This function allows a completion script to delegate its work to another command's completion script. This is perfect for commands that are aliases or wrappers for other commands.

Usage: `command -f completion//reexecute other-command`

For example, the completion script for `bg` is simply:

```sh
# share/completion/bg
function completion/bg {
    command -f completion//reexecute jobs
}
```

## Providing Completions for Operands

When `$ARGOPT` is not `-`, your script is responsible for providing the completion candidates for command arguments. You do this using the shell's built-in `complete` command.

Common uses include:

- `complete -f`: Complete file names.
- `complete -d`: Complete directory names.
- `complete -u`: Complete user names.
- `complete -g`: Complete group names.
- `complete -j`: Complete job specs.
- `complete -- ...`: Complete a specific list of words.

You can have any custom logic to generate the list of words to complete.

It is important to pass the `-P "$PREFIX"` option to `complete`. This ensures that completion candidates are generated based on the non-prefix part of the word being completed.
