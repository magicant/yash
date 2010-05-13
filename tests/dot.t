# used by builtin.p.tst and builtin.y.tst as a sourced script
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo -"$@"-

count=$#
if [ $# -ne 0 ]; then
    set --
    . ./dot.t
fi

echo returning -"$@"-
if true; then
    return
fi
echo not reached
