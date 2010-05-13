# path.y.tst: yash-specific test of pathname handling
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo ===== cd builtin =====

cd --default-directory="$TESTTMP"
if [ x"$PWD" = x"$TESTTMP" ]; then
    echo cd --default-directory=\$TESTTMP
fi

mkdir -p ./-
cd --default-directory="-"
if [ x"$PWD" = x"$TESTTMP/-" ]; then
    echo cd --default-directory=-
fi

cd - >/dev/null
rmdir ./-
