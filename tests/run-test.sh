#!/bin/sh

# run-test.sh: runs tests specified by $TESTEE and $TEST_ITEMS
# (C) 2007-2008 magicant
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


echo "Testing ${TESTEE:=../yash} for ${TEST_ITEMS:=*.tst}"
echo "Any output from the tests indicates a possible malfunction"

# make temporary directory
: ${TMPDIR:=/tmp}
if [ x"${TMPDIR}" = x"${TMPDIR#/}" ]; then
    TMPDIR=/tmp
fi
TESTTMP="${TMPDIR}/yashtest.$$"
if ! mkdir -m u=rwx,go= "$TESTTMP"; then
    echo Cannot create temporary directory
    exit 1
fi
trap 'rm -rf $TESTTMP' EXIT

LC_ALL=C
export INVOKE TESTEE LC_ALL TESTTMP
unset ENV IFS failed

failed=0
for x in $TEST_ITEMS
do
    x="${x%.tst}"
    echo " * $x"

    if [ x"$x" = x"${x%.p}" ]
    then INVOKE=
    else INVOKE='./invoke sh'
    fi

    $INVOKE $TESTEE "$x.tst" >|"${TESTTMP}/test.out" 2>|"${TESTTMP}/test.err"

    failure=0
    if ! diff "${x}.out" "${TESTTMP}/test.out"
    then
	failure=1
    fi
    if
	if [ -f "${x}.err" ]
	then
	    ! diff "${x}.err" "${TESTTMP}/test.err"
	else
	    ! diff /dev/null "${TESTTMP}/test.err"
	fi
    then
	failure=1
    fi
    if [ 0 -ne $failure ]; then failed=$(( failed + 1 )); fi
done

if [ 0 -eq $failed ]
then
    echo "All tests successful."
else
    echo "${failed} test(s) failed."
    false
fi


# vim: set ts=8 sts=4 sw=4 noet:
