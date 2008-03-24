#!/bin/sh

TESTEE=${1:-../yash}
export LC_ALL=C LANG=C TESTEE

echo "Testing ${TESTEE}"
echo "Any output from any test indicates a possible malfunction"

failed=0
for x in *.tests
do
    echo $x...
    if ! ${TESTEE} $x 2>&1 | diff - ${x%.tests}.right
    then
	failed=$(( ${failed} + 1 ))
    fi
done

echo "${failed} test(s) failed."
test 0 -eq ${failed}


# vim: set ts=8 sts=4 sw=4 noet:
