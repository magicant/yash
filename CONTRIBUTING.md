# Contributing to Yash

Thank you for your interest in contributing to Yash. This document provides information to help you get started with participating in this project.

Note: The current maintainer has devoted more attention to yash-rs than to yash itself. As a result, yash development has not been as active recently.

## Asking Questions About Yash

If you have questions about how to use yash or how to develop it, please use the Discussions forum.

## Reporting Bugs in Yash

If you encounter any issues with yash's behavior, please raise a question in Discussions. We strongly recommend using Discussions before creating new Issues. Only create an Issue when you are certain that yash's behavior is incorrect and requires correction.

## Proposing New Features or Behavior Changes

Please use the provided template to create a new Issue.

## Modifying Yash's Code

First, note that yash is licensed under the GNU General Public License version 2. Any code you contribute will also be subject to the GPL version 2.

The build instructions for yash are documented in the `INSTALL` file. Using `--debug` when running `configure` will set up a more debug-friendly configuration.

Running `make` will build the `yash` binary in the root directory. You can directly execute this binary for debugging purposes. However, be aware that scripts in the `share` directory will not be loaded unless you either install all files using `make install` or modify the `YASH_LOADPATH` variable at runtime.

Automated tests are located in the `tests` directory. You can run the tests using `make check`. For detailed instructions, refer to `tests/README.md`. Please add test cases to verify the functionality of your implemented features or fixes.

Please adhere to the existing coding style when making code changes.

When changes are likely to be significant, we recommend seeking the maintainer's advice in Discussions or Issues before starting the implementation.

## Contributing to Yash Documentation

Yash's documentation is written in [AsciiDoc]. Running `make docs` with the `asciidoc` tool installed will convert the documentation from text to HTML.

## Contributing to Command Line Argument Completion

The scripts defining command line argument completion is located in the `share/completion` directory. The common functions used by the completion scripts are defined in the `INIT` file.

## Translating Messages Printed by Yash

Yash uses [GNU gettext] to localize printed messages.

To add translation for a new language:

1. Fork and clone this repository.
2. Install `make` and `gettext`.
3. Run `./configure`.
4. Enter the `po` directory and run `make`.
5. Run `msginit`. This command creates a new po file with a name corresponding to your current locale.
6. Edit the po file adding translation for each message.
7. Commit the file, push it to GitHub, and make a pull request.

To update existing translation, follow the steps above except that
you edit the existing po file instead of creating a new one with `msginit`.
Outdated translations are marked as `fuzzy` by the `gettext` tool.
Remove the `fuzzy` mark and correct the translation.

[AsciiDoc]: https://asciidoc.org/
[GNU gettext]: https://www.gnu.org/software/gettext/
