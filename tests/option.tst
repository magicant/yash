tmp=${TESTTMP}/option
mkdir -p "$tmp"

echo ===== nocaseglob =====
set --nocaseglob
echo O[OPQ]T*ON.tst
set +o nocaseglob
echo O[OPQ]T*ON.tst

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
echo n*ll 1
set +o nullglob
echo n*ll 2

# test of braceexpand is in "expand.tst"

cd - >/dev/null

echo ===== posix =====
$INVOKE $TESTEE --posix -c 'echo "$PS2"'

rm -fr "$tmp"
