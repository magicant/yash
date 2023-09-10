Yash: yet another shell
=======================

https://magicant.github.io/yash/
This README is also available in [日本語](README.ja.md).


Yash, yet another shell, is a POSIX-compliant command line shell
written in C99 (ISO/IEC 9899:1999). Yash is intended to be the most
POSIX-compliant shell in the world while supporting features for daily
interactive and scripting use. Notable features are:

 * Global aliases
 * Arrays
 * Socket redirection, pipeline redirection, and process redirection
 * Brace expansion and extended globbing
 * Fractional numbers in arithmetic expansion
 * Prompt command and command-not-found handler
 * Command line completion with predefined completion scripts for more
   than 100 commands
 * Command line prediction based on command history

Yash can be modified/redistributed under the terms of the GNU General
Public License (Version 2) but the use of this program is without any
warranty. See the [COPYING](COPYING) file for the full text of GPL.

Yash is a [GitHub-hosted project](https://github.com/magicant/yash), and
used to be an [OSDN-hosted project](https://osdn.jp/projects/yash/).


## Current Development Status

Yash now fully supports POSIX.1-2008 (IEEE Std 1003.1, 2016 Edition)
except for the limitations listed below.

Yash is stable. A maintenance update is released every three months or
so. For the change history, see the [NEWS](NEWS) file.


## Requirements

Yash is supposed to build and run on any POSIX.1-2001 environment with
the Software Development Utilities and the C-Language Development
Utilities options.

Currently, yash is mainly tested on Fedora, macOS, and Cygwin.


## Installation

See the [INSTALL](INSTALL) file to see how to build and install yash.

After installation, the manual can be viewed by

    $ man yash

The manual is also available online at
<https://magicant.github.io/yash/doc/>.


## Basic Configuration

Below is a description of basic configuration that you might want to
see after installation to get started with yash. For configuration
details, see the manual.

### Initialization scripts

When yash is started as a login shell, it reads `~/.yash_profile`. This
file is a shell script in which you define environment variables using
the export command.

When yash is started for an interactive use, it reads `~/.yashrc` (after
reading `~/.yash_profile` if it is a login shell also). In this file,
you make other configurations such as aliases, prompt strings, key
bindings for command line editing, and command-not-found handler.
Use the [share/initialization/sample](share/initialization/sample) file
as a template for your `~/.yashrc`.

### Making yash your login shell

In many Unix-like OSes, a shell must be listed in `/etc/shells` to be
set as a login shell. Edit this file and ensure that the path to yash
is written in the file.

Then, run the `chsh` command in the terminal and follow instructions
from the command. Depending on your system, you may have to use
another command to change the login shell. See documentation on your
system.


## Implementation Notes

 * In C, a null character represents the end of a string. If input to
   the shell itself contains a null character, characters following
   the null character will be ignored.
 * The GCC extension keyword `__attribute__` is used in the source
   code. When not compiled with GCC, this keyword is removed by the
   preprocessor, so generally there is no harm. But if your compiler
   uses this keyword for any other purpose, compilation may fail.
   Additionally, some other identifiers starting with `_` may cause
   compilation errors on some rare environments.
 * Some signals are assumed to have the specific numbers:
     SIGHUP=1 SIGINT=2 SIGQUIT=3 SIGABRT=6
     SIGKILL=9 SIGALRM=14 SIGTERM=15
 * POSIX disallows non-interactive shells to ignore or catch SIGTTIN,
   SIGTTOU, and SIGTSTP by default. Yash, however, ignores these
   signals if job-control is enabled, even if non-interactive.
 * File permission flags are assumed to have the specific values:
   ```
   0400=user read    0200=user write   0100=user execute
   0040=group read   0020=group write  0010=group execute
   0004=other read   0002=other write  0001=other execute
   ```
 * The character categorization in locales other than the POSIX locale
   is assumed upward compatible with the POSIX locale.
 * The `-o nolog` option is not supported: it is silently ignored.
 * According to POSIX, the value of variable `PS1` is subject to
   parameter expansion. Yash performs command substitution and
   arithmetic expansion as well on the `PS1` value.
 * According to POSIX, the command `printf %c foo` should print the
   first byte of string `foo`. Yash prints the first character of
   `foo`, which may be more than one byte.
 * The `return` built-in, if executed in a trap, can operate only on a
   function, script, or loop that has been executed within the trap.
   This limitation is not strictly POSIX-compliant, but needed for
   consistent and predictable behavior of the shell.
 * Results of pathname expansion is sorted only by collating sequence
   of the current locale. If the collating sequence does not have a
   total ordering of characters, order of uncomparable results are
   unstable. This limitation is not strictly POSIX-compliant, but
   inevitable due to use of wide characters in the whole shell.


## Known Issues

 * Line number (`$LINENO`) may not be counted correctly in and after a
   complex expansion containing a line continuation.
 * Non-ASCII characters may not be correctly handled in some locales
   on Solaris. This may be worked around by undefining the
   `HAVE_WCSNRTOMBS` macro in the `config.h` header file.


## Contributions

Comments, suggestions, and bug reports are welcome at:

 * [Issue tracking system](https://github.com/magicant/yash/issues)
 * [Discussion forum](https://github.com/magicant/yash/discussions)

If you are interested in translation, please refer to
[TRANSLATING.md](TRANSLATING.md).


----------------------
Watanabe, Yuki <magicant@wonderwand.net>
