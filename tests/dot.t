# used by builtin.p.tst and builtin.tst as a sourced script

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
