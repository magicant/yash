This directory includes automated test cases for yash.

The test cases are grouped into POSIX tests and yash-specific tests, which
are written in files named `*-p.tst` and `*-y.tst`, respectively. Every POSIX
shell is supposed to pass the POSIX tests, so those test cases does not
test any yash-specific behavior at all. To run the POSIX tests on a shell
other than yash, run in this directory:

    $ make TESTEE=<shell_command_name> test-posix

---------------------------------------------------------------------------

Some test cases are skipped by the test runner depending on the
configuration of yash, user's privilege, etc. If the help built-in is
disabled in the configuration, for example, tests for the help built-in are
skipped. There is no configuration in which no tests are skipped; some
tests require a root privilege while some require a non-root privilege.

---------------------------------------------------------------------------

Test cases can be run in parallel if your make supports parallel build.
Exceptionally, test cases that require a control terminal have to be run
sequentially if a pseudo-terminal cannot be opened to obtain a control
terminal that can be used for testing. In that case, you have to run the
tests in the foreground process group so that they can make use of the
current control terminal. Test case files containing such tests are marked
with the `%REQUIRETTY%` keyword.

---------------------------------------------------------------------------

To test yash with Valgrind, run `make test-valgrind` in this directory.
Yash must have been built without enabling any of the following variables
in `config.h`:

 * `HAVE_PROC_SELF_EXE`
 * `HAVE_PROC_CURPROC_FILE`
 * `HAVE_PROC_OBJECT_AOUT`

Otherwise, some tests would fail after Valgrind is invoked as a shell where
yash should be invoked.

Some tests are skipped to avoid false failures.
