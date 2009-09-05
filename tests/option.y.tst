tmp="${TESTTMP}/option"
mkdir -p "$tmp"

echo ===== -h =====
set -h
echo "$-" | grep -Fq h || echo 'no h in $-' >&2
hash -r
hoptiontest () {
	cat /dev/null
}
echo $(hash | grep /cat\$ | wc -l)
set +h

echo ===== nocaseglob =====
set --nocaseglob
echo O[OPQ]T*ON.y.tst
set +o nocaseglob
echo O[OPQ]T*ON.y.tst

cd "$tmp"

echo ===== dotglob =====
touch .dotfile
set --dotglob
echo *
set +o dotglob
echo *

echo ===== markdirs =====
mkdir dir
set --markdirs
echo di*
set +o markdirs
echo di*

echo ===== extendedglob =====
mkdir dir/dir2
touch dir/dir2/file
set --extendedglob
echo **/file
set +o extendedglob
echo **/file

echo ===== nullglob =====
set --nullglob
echo n*ll f[o/b]r f?o/b*r 1
set +o nullglob
echo n*ll f[o/b]r f?o/b*r 2

# test of braceexpand is in "expand.tst"

cd - >/dev/null

echo ===== posix =====
$INVOKE $TESTEE --posix -c 'echo "$PS2"'

rm -fr "$tmp"
