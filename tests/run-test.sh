# run-test.sh: runs tests specified by $TESTEE and $TEST_ITEMS
# (C) 2007-2011 magicant
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


# log file
rm -f "${logfile:=test.log}"
exec 3>&1
exec >"${logfile}"
echo "========== Yash Test Log =========="
echo
printf 'Test started at: '
LC_TIME=C date
echo

# make temporary directory
: ${TMPDIR:=$PWD}
case "$TMPDIR" in
    /*) ;;
    *)  echo "\$TMPDIR is not an absolute path"
	echo "\$TMPDIR must be an absolute path" >&2
	exit 1
esac
case "$TMPDIR" in
    */) TESTTMP=${TMPDIR}test.$$ ;;
    *)  TESTTMP=${TMPDIR}/test.$$ ;;
esac
trap 'rm -rf "$TESTTMP"' EXIT
trap 'rm -rf "$TESTTMP"; exit 1' HUP INT QUIT ABRT ALRM TERM PIPE USR1 USR2
printf 'Test directory is: %s\n' "$TESTTMP"
if ! mkdir -m u=rwx,go= "$TESTTMP"; then
    echo Cannot create temporary directory
    echo Cannot create temporary directory >&2
    trap - EXIT
    exit 1
fi

printf 'uname -i = '; uname -i 2>/dev/null || echo \?
printf 'uname -n = '; uname -n 2>/dev/null || echo \?
printf 'uname -m = '; uname -m 2>/dev/null || echo \?
printf 'uname -o = '; uname -o 2>/dev/null || echo \?
printf 'uname -p = '; uname -p 2>/dev/null || echo \?
printf 'uname -r = '; uname -r 2>/dev/null || echo \?
printf 'uname -s = '; uname -s 2>/dev/null || echo \?
printf 'uname -v = '; uname -v 2>/dev/null || echo \?
echo "PATH=$PATH"

export INVOKE TESTEE TESTTMP
export LC_MESSAGES=POSIX LC_CTYPE="${LC_ALL-${LC_CTYPE-${LANG}}}" LANG=POSIX
unset CDPATH ENV HISTFILE HISTSIZE MAIL MAILCHECK MAILPATH IFS LC_ALL
unset YASH_AFTER_CD COMMAND_NOT_FOUND_HANDLER HANDLED PROMPT_COMMAND
umask u=rwx,go=

checkskip()
case "$1" in
    alias.y|alias.p)
	$INVOKE $TESTEE -c 'PATH=; alias' >/dev/null 2>&1
	;;
    array.y)
	$INVOKE $TESTEE -c 'type array' 2>/dev/null | \
	    grep '^array: a regular built-in' >/dev/null
	;;
    dirstack.y)
	$INVOKE $TESTEE -c 'type pushd' 2>/dev/null | \
	    grep '^pushd: a regular built-in' >/dev/null
	;;
    job.y)
	# ensure that /dev/tty is available and that we're in foreground
	./checkfg
	;;
    help.y)
	$INVOKE $TESTEE -c 'type help' 2>/dev/null | \
	    grep '^help: a regular built-in' >/dev/null
	;;
    history.y)
	HISTFILE= $INVOKE $TESTEE -i +m --norcfile -c 'PATH=; fc -l' \
	    >/dev/null 2>&1
	;;
    lineedit.y)
	$INVOKE $TESTEE -c 'type bindkey' 2>/dev/null | \
	    grep '^bindkey: a regular built-in' >/dev/null
	;;
    printf.y)
	$INVOKE $TESTEE -c 'type printf' 2>/dev/null | \
	    grep '^printf: a regular built-in' >/dev/null
	;;
    test.y)
	$INVOKE $TESTEE -c 'type test' 2>/dev/null | \
	    grep '^test: a regular built-in' >/dev/null
	;;
esac

printf 'Effective user ID: %s\n' "${EUID=$(id -u)}"
if [ "$EUID" -eq 0 ]; then
    isroot=true
else
    isroot=false
fi
if diff -u /dev/null /dev/null >/dev/null 2>&1; then
    diff='diff -u'
elif diff -c /dev/null /dev/null >/dev/null 2>&1; then
    diff='diff -c'
else
    diff='diff'
fi
diffresult() {
    if $isroot && [ -r "$x.$2" ]; then
	y="$x.$2"
    elif [ -r "$x.$1" ]; then
	y="$x.$1"
    else
	y="/dev/null"
    fi
    echo "Diff to the expected output:"
    $diff "$y" "${TESTTMP}/test.$1"
}

echo
printf 'Testing %s for %s\n' "${TESTEE:=../yash}" "${TEST_ITEMS:=*.tst}"
printf 'Testing %s for %s\n' "${TESTEE:=../yash}" "${TEST_ITEMS:=*.tst}" >&3

failed=0
for x in $TEST_ITEMS
do
    x="${x%.tst}"
    printf '%-12s' "$x" >&3

    echo
    echo
    echo ======================================================================
    printf '==========                 %-16s                 ==========\n' "$x"
    echo ======================================================================
    echo

    case "$x" in
	*.p) INVOKE='./invoke sh' ;;
	*  ) INVOKE=              ;;
    esac
    if ! checkskip "$x"
    then
	echo " * SKIPPED *"
	echo
	echo "skipped" >&3
	continue
    fi

    $INVOKE $TESTEE "$x.tst" \
	>|"${TESTTMP}/test.out" 2>|"${TESTTMP}/test.err" 3>&-

    echo ">>>>>>>>>>>>>>> $x.tst STDOUT >>>>>>>>>>>>>>>"
    cat "${TESTTMP}/test.out" 
    echo "<<<<<<<<<<<<<<< $x.tst STDOUT <<<<<<<<<<<<<<<"
    echo
    diffresult out oux
    outresult=$?

    echo
    echo ">>>>>>>>>>>>>>> $x.tst STDERR >>>>>>>>>>>>>>>"
    cat "${TESTTMP}/test.err" 
    echo "<<<<<<<<<<<<<<< $x.tst STDERR <<<<<<<<<<<<<<<"
    echo
    diffresult err erx
    errresult=$?

    if [ $outresult -eq 0 ] && [ $errresult -eq 0 ]
    then
	echo ok >&3
    else
	echo FAILED >&3
	: $(( failed += 1 ))
    fi
done

echo
echo ======================================================================
echo ======================================================================
echo
printf 'Test finished at: '
LC_TIME=C date
echo

if [ 0 -eq $failed ]
then
    printf '%s passed all of %s.\n' "${TESTEE}" "${TEST_ITEMS}"
    printf '%s passed all of %s.\n' "${TESTEE}" "${TEST_ITEMS}" >&3
else
    printf '%s failed %d of %s.\n' "${TESTEE}" "${failed}" "${TEST_ITEMS}"
    printf '%s failed %d of %s.  See test.log for more info.\n' \
	"${TESTEE}" "${failed}" "${TEST_ITEMS}" >&3
    false
fi


# vim: set ts=8 sts=4 sw=4 noet:
