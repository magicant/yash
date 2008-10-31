tmp="${TESTTMP}/dirstack.y"

mkdir "$tmp"
mkdir "$tmp/1" "$tmp/2" "$tmp/3" "$tmp/4"

cd "$tmp"
pushd "$tmp/1"; touch f1
pushd "$tmp/2"; touch f2
pushd "$tmp/3"; touch f3
pushd "$tmp/4"; touch f4
echo *; popd >/dev/null
echo *; popd >/dev/null
echo *; popd >/dev/null
echo *; popd >/dev/null

if [ x"$PWD" = x"$tmp" ]; then
	echo ok
fi
echo */*
pushd 1
dirs -c
cd - >/dev/null
echo */*

echo ===== 1 =====

pushd "$tmp/1"
pushd "$tmp/2"
pushd "$tmp/3"
pushd "$tmp/4"
dirs | sed -e 's;/.*/;;'
dirs -v | sed -e 's;/.*/;;'

echo ===== 2 =====

pushd
dirs | sed -e 's;/.*/;;'
pushd
dirs | sed -e 's;/.*/;;'

echo ===== 3 =====

pushd +3
pushd -2
dirs | sed -e 's;/.*/;;'

echo ===== 4 =====

popd +1
popd -2
dirs | sed -e 's;/.*/;;'

echo ===== 5 =====

pushd +0
pushd -2
dirs | sed -e 's;/.*/;;'

echo ===== 6 =====

unset DIRSTACK
popd >/dev/null 2>/dev/null
echo exit $?

cd "$tmp"
(
cd "$tmp/1"
pushd - | sed -e 's;/.*/;;'
)

pushd "$tmp/1"
readonly DIRSTACK
pushd "$tmp/2" 2>/dev/null
echo exit $?
dirs | sed -e 's;/.*/;;'
