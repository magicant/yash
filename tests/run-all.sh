#!/bin/sh

if [ x"$1" = x"-p" ]; then
    scripts='*.posix.test'
    shift
else
    scripts='*.test'
fi

TESTEE=${1:-../yash}
LC_ALL=C
IFS=' 	
'
export TESTEE LC_ALL IFS

echo "Starting test of ${TESTEE}"
echo "Any output from the tests indicates a possible malfunction"

failed=0
for x in $scripts
do
    echo ${x%.test}...
    if ! ${TESTEE} $x 2>&1 | diff - ${x%.test}.right
    then
	failed=$(( failed + 1 ))
    fi
done

if [ 0 -eq ${failed} ]
then
    echo "All tests successful."
else
    echo "${failed} test(s) failed."
    false
fi


# vim: set ts=8 sts=4 sw=4 noet:
