# builtin.p.tst: test of builtins for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

savepath=$PATH

echo ===== : true false =====

:
echo $?
true
echo $?
! false # "false" may return any non-zero status
echo $?

(
# a function may override a non-special builtin
false () { true; }
if false; then echo false true; fi

# a non-regular builtin should be executed if not in PATH
PATH=
if : && true && false; then
    PATH=$savepath
    echo : true false
else
    PATH=$savepath
fi
)

echo ===== return break continue =====

retfunc () {
    return 3
    echo retfunc ng
}
retfunc
echo $?

retfunc () {
    (exit 4)
    return
    echo retfunc ng 2
}
retfunc
echo $?

retfunc () {
    retfunc_inner () {
	return $1
    }
    retfunc_inner 5
    retfunc_inner=$?
    echo retfunc
    return $retfunc_inner
}
retfunc
echo $?

while true; do
    echo while ok
    break
    echo while ng
done
until false; do
    echo until ok
    break
    echo until ng
done
for i in 1 2 3 4; do
    echo for $i
    break
    echo for ng
done

i=0
while [ $i -eq 0 ]; do
    echo while $i
    i=1
    continue
    echo while ng
done
i=0
until [ $i -ne 0 ]; do
    echo until $i
    i=1
    continue
    echo until ng
done
for i in 1 2 3 4; do
    echo for $i
    continue
    echo for ng
done

for i in 1 2 3; do
    for j in 7 8 9; do
	echo $i $j
	if [ $i -eq 3 ]; then
	    break 2
	elif [ $j -eq 8 ]; then
	    continue 2
	fi
    done
done

k=0
for i in 1 2 3; do
    if true; then
	while true; do
	    until false; do
		case $i in
		    1)
		    for j in 7 8 9; do
			echo $i $j $k
			if [ $k -ne 0 ]; then
			    break 3
			fi
			k=1
		    done
		    continue 3
		    ;;
		    2)
		    while true; do
			until false; do
			    echo i=2
			    break 4
			done
		    done
		    ;;
		    *)
		    echo i=3
		    break 999
		esac
	    done
	done
    fi
    echo !
done
echo done


echo ===== exit =====

$INVOKE $TESTEE <<\END
r () { return $1; }
trap 'r 10' EXIT
exit
END
echo exit 1 $?

$INVOKE $TESTEE <<\END
r () { return $1; }
trap 'r 10' EXIT
exit 20
END
echo exit 2 $?

$INVOKE $TESTEE <<\END
r () { return $1; }
trap 'r 10; exit' EXIT
exit
END
echo exit 3 $?

$INVOKE $TESTEE <<\END
r () { return $1; }
trap 'r 10; exit' EXIT
exit 20
END
echo exit 4 $?

$INVOKE $TESTEE <<\END
r () { return $1; }
trap 'r 10; exit 30' EXIT
exit
END
echo exit 5 $?

$INVOKE $TESTEE <<\END
r () { return $1; }
trap 'r 10; exit 30' EXIT
exit 20
END
echo exit 6 $?


echo ===== . =====

. ./dot.t
echo $count


echo ===== command =====

echo () { :; }
command echo ok

unset -f echo
PATH=
command -p echo PATH=
PATH=$savepath

if command -v echo >/dev/null 2>&1; then
    commandv='command -v'
    if ! command -v echo | grep '^/' | grep '/echo$' >/dev/null; then
	echo "\"command -V echo\" doesn't return a fullpath" >&2
    fi
    if ! command -v ./invoke | grep '^/' | grep '/invoke$' >/dev/null; then
	echo "\"command -V ./invoke\" doesn't return a fullpath" >&2
    fi
    if command -v _no_such_command_; then
	echo "\"command -v _no_such_command_\" returned zero status" >&2
    fi
else
    commandv='echo'
fi
$($commandv echo) command -v
$commandv retfunc
$commandv if
$commandv then
$commandv else
$commandv elif
$commandv fi
$commandv do
$commandv done
$commandv case
$commandv esac
$commandv while
$commandv until
$commandv for
$commandv {
$commandv }
$commandv !
$commandv in
[ x"$($commandv cat)" = x"$($commandv $($commandv cat))" ] ||
echo "\"command -v\" not idempotent" >&2

if command -V echo >/dev/null 2>&1; then
    if ! command -V echo | grep -F "$(command -v echo)" >/dev/null; then
	echo "\"command -V echo\" doesn't include a fullpath" >&2
	return 1
    fi
fi

(
command command exec >/dev/null
echo not printed
)
command -p cat -u /dev/null


echo ===== special builtins =====
(: <"${TESTTMP}/no.such.file"; echo not reached - redir) 2>/dev/null
(readonly ro=ro; ro=xx eval 'echo test'; echo not reached - assign) 2>/dev/null
(unset unset; set ${unset?}; echo not reached - expansion) 2>/dev/null
(. "${TESTTMP}/no.such.file"; echo not reached - dot not found) 2>/dev/null
(break invalid argument; echo not reached - usage error) 2>/dev/null


echo ===== exec =====

$INVOKE $TESTEE -c 'exec echo exec'
$INVOKE $TESTEE -c 'exec /dev/null' 2>/dev/null
echo $?

exec echo exec echo
echo not reached
