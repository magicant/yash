# input-p.tst: test of input processing for any POSIX-compliant shell

# Note that this test case depends on the fact that run-test.sh passes the
# input using a regular file. The test would fail if the input was not
# seekable. See also the "Input files" section in POSIX.1-2008, 1.4 Utility
# Description Defaults.
test_oE 'no input more than needed is read'
head -n 1
echo - this line is consumed by head
echo - this line is consumed by shell
__IN__
echo - this line is consumed by head
- this line is consumed by shell
__OUT__

test_x -e 0 'exit status of empty input'
__IN__

test_x -e 0 'exit status of input containing blank lines only'


__IN__

test_x -e 0 'exit status of input containing blank lines and comments only'

# foo

# bar
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:
