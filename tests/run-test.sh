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


echo "Testing ${TESTEE:=../yash} for ${TEST_ITEMS:?}"
echo "Any output from the tests indicates a possible malfunction"

LC_ALL=C
export INVOKE TESTEE LC_ALL
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
    if ! $INVOKE $TESTEE "${x}.tst" 2>&1 | diff - "${x}.out"
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
