# set-p.tst: test of the set built-in for any POSIX-compliant shell

posix="true"

# TODO setting positional parameters and/or shell options

# This test assumes that the output from "set -o" is always the same for the
# same option configuration.
test_OE -e 0 'set -o/+o'
set -aeu
set -o > saveset
saveset=$(set +o)
set +aeu -f
eval "$saveset"
set -o | diff saveset -
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:
