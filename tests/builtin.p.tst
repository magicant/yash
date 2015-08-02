# builtin.p.tst: test of builtins for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

savepath=$PATH

echo ===== : true false =====

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

case "$(command -v echo)" in
    /*/echo | /echo ) ;;
    *               ) echo "\$(command -v echo) = $(command -v echo)"
esac
case "$(command -v ./invoke)" in
    /*/invoke ) ;;
    *         ) echo "\$(command -v ./invoke) = $(command -v ./invoke)"
esac
if PATH= command -v _no_such_command_; then
    echo "\"command -v _no_such_command_\" returned zero status" >&2
fi
$(command -v echo) command -v
command -v retfunc
command -v if
command -v then
command -v else
command -v elif
command -v fi
command -v do
command -v done
command -v case
command -v esac
command -v while
command -v until
command -v for
command -v {
command -v }
command -v !
command -v in
[ x"$(command -v cat)" = x"$(command -v $(command -v cat))" ] ||
echo "\"command -v\" not idempotent" >&2

# output from "command -V echo" must include path for "echo"
case "$(command -V echo)" in
    *$(command -v echo)*) ;;
    *) echo "\$(command -V echo) = $(command -V echo)" ;;
esac

(
command command exec >/dev/null
echo not printed
)
$INVOKE $TESTEE 2>/dev/null <<\END
command exec 3<$TESTTMP/no.such.file
echo ok
exec 3<$TESTTMP/no_such_file
echo not printed
END
PATH= command -p cat -u /dev/null


echo ===== special builtins =====

{
    (: <"${TESTTMP}/no.such.file"; echo not reached - redir)
    (readonly ro=ro; ro=xx eval 'echo test'; echo not reached - assign)
    (unset unset; set ${unset?}; echo not reached - expansion)
    (. "${TESTTMP}/no.such.file"; echo not reached - dot not found)
    (break invalid argument; echo not reached - usage error)
} 2>/dev/null


echo ===== exec =====

$INVOKE $TESTEE -c 'exec echo exec'
$INVOKE $TESTEE -c '(exec echo 1); exec echo 2'

exec echo exec echo
echo not reached
