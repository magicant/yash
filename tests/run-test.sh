# run-test.sh: runs tests specified by $TESTEE and $TEST_ITEMS
# (C) 2007-2009 magicant
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


# make temporary directory
: ${TMPDIR:=$PWD}
if [ x"${TMPDIR}" = x"${TMPDIR#/}" ]; then
    echo \$TMPDIR must be an absolute path >&2
    exit 1
fi
TESTTMP="$(cd ${TMPDIR}; pwd)/test.$$"
trap 'rm -rf $TESTTMP' EXIT HUP INT QUIT ABRT ALRM TERM PIPE USR1 USR2
if ! mkdir -m u=rwx,go= "$TESTTMP"; then
    echo Cannot create temporary directory >&2
    trap - EXIT
    exit 1
fi

echo "Testing ${TESTEE:=../yash} for ${TEST_ITEMS:=*.tst}"
echo "Any output from the tests indicates a possible malfunction"

export INVOKE TESTEE TESTTMP
export LC_MESSAGES=POSIX LC_CTYPE="${LC_ALL-${LC_CTYPE-${LANG}}}" LANG=POSIX
unset ENV HISTFILE HISTSIZE MAIL MAILCHECK MAILPATH IFS LC_ALL failed
umask u=rwx,go=

checkskip()
case "$1" in
    alias.y|alias.p)
	$INVOKE $TESTEE -c 'PATH=; alias' >/dev/null 2>&1
	;;
    dirstack.y)
	$INVOKE $TESTEE -c 'type pushd' 2>/dev/null | \
	    grep '^pushd: regular builtin' >/dev/null
	;;
    history.y)
	HISTFILE= $INVOKE $TESTEE -i --norcfile -c 'PATH=; fc -l' \
	    >/dev/null 2>&1
	;;
    printf.y)
	$INVOKE $TESTEE -c 'type printf' 2>/dev/null | \
	    grep '^printf: regular builtin' >/dev/null
	;;
    test.y)
	$INVOKE $TESTEE -c 'type test' 2>/dev/null | \
	    grep '^test: regular builtin' >/dev/null
	;;
esac

failed=0
for x in $TEST_ITEMS
do
    x="${x%.tst}"
    if [ x"$x" = x"${x%.p}" ]
    then INVOKE=
    else INVOKE='./invoke sh'
    fi
    if ! checkskip "$x"
    then
	echo " * $x (skipped)"
	continue
    fi

    echo " * $x"
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