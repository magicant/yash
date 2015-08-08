# builtin.p.tst: test of builtins for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

savepath=$PATH

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

func() { :; }

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
command -v func
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
