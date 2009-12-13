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
