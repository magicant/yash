# fulltest.sh: runs tests for many combinations of configuration options
# (C) 2010 magicant

do_test () {
	if [ -r Makefile ]; then
		$MAKE clean
	fi
	printf '\n========== ./configure %s\n' "$*"
	./configure "$@"
	$MAKE test
}

set -o errexit

echo "$0: using '${MAKE:=make}' as make"

a0='' a1='--disable-lineedit' a2='--disable-history --disable-lineedit' a3=''
b0='' b1='--disable-alias'
c0='' c1='--disable-array'
d0='' d1='--disable-dirstack'
e0='' e1='--disable-help'
f0='' f1='--disable-printf'
g0='' g1='--disable-socket'
h0='' h1='--disable-test'
i0='' i1='--disable-ulimit'
j0='' j1='--debug'
k0='' k1=''
l0='' l1=''
m0='' m1=''

do_test $a0 $b0 $c0 $d0 $e0 $f0 $g0 $h0 $i0 $j0 $k0 $l0 $m0 "$@"
do_test $a0 $b0 $c0 $d0 $e0 $f1 $g1 $h1 $i1 $j1 $k1 $l1 $m1 "$@"
do_test $a0 $b1 $c1 $d1 $e1 $f0 $g0 $h0 $i0 $j1 $k1 $l1 $m1 "$@"
do_test $a0 $b1 $c1 $d1 $e1 $f1 $g1 $h1 $i1 $j0 $k0 $l0 $m0 "$@"
do_test $a1 $b0 $c0 $d1 $e1 $f0 $g0 $h1 $i1 $j0 $k0 $l1 $m1 "$@"
do_test $a1 $b0 $c0 $d1 $e1 $f1 $g1 $h0 $i0 $j1 $k1 $l0 $m0 "$@"
do_test $a1 $b1 $c1 $d0 $e0 $f0 $g0 $h1 $i1 $j1 $k1 $l0 $m0 "$@"
do_test $a1 $b1 $c1 $d0 $e0 $f1 $g1 $h0 $i0 $j0 $k0 $l1 $m1 "$@"
do_test $a2 $b0 $c1 $d0 $e1 $f0 $g1 $h0 $i1 $j0 $k1 $l0 $m1 "$@"
do_test $a2 $b0 $c1 $d0 $e1 $f1 $g0 $h1 $i0 $j1 $k0 $l1 $m0 "$@"
do_test $a2 $b1 $c0 $d1 $e0 $f0 $g1 $h0 $i1 $j1 $k0 $l1 $m0 "$@"
do_test $a2 $b1 $c0 $d1 $e0 $f1 $g0 $h1 $i0 $j0 $k1 $l0 $m1 "$@"
do_test $a3 $b0 $c1 $d1 $e0 $f0 $g1 $h1 $i0 $j0 $k1 $l1 $m0 "$@"
do_test $a3 $b0 $c1 $d1 $e0 $f1 $g0 $h0 $i1 $j1 $k0 $l0 $m1 "$@"
do_test $a3 $b1 $c0 $d0 $e1 $f0 $g1 $h1 $i0 $j1 $k0 $l0 $m1 "$@"
do_test $a3 $b1 $c0 $d0 $e1 $f1 $g0 $h0 $i1 $j0 $k1 $l1 $m0 "$@"
