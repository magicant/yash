# help.y.tst: yash-specific test of the help builtin
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

help
echo ===== 1 $?
help help help
echo ===== 2 $?
help : true false set cd pwd hash umask >/dev/null
echo ===== 3 $?
help typeset export readonly unset shift getopts read >/dev/null
echo ===== 4 $?
help trap kill jobs fg bg wait disown >/dev/null
echo ===== 5 $?
help return break continue eval . exec command type times exit suspend>/dev/null
echo ===== 6 $?
case "$(type alias 2>/dev/null)" in *builtin*)
    help alias unalias >/dev/null
esac
echo ===== 7 $?
case "$(type array 2>/dev/null)" in *builtin*)
    help array >/dev/null
esac
echo ===== 8 $?
case "$(type pushd 2>/dev/null)" in *builtin*)
    help pushd popd dirs >/dev/null
esac
echo ===== 9 $?
case "$(type fc 2>/dev/null)" in *builtin*)
    help fc history >/dev/null
esac
echo ===== 10 $?
case "$(type ulimit 2>/dev/null)" in *builtin*)
    help ulimit >/dev/null
esac
echo ===== 11 $?
case "$(type echo 2>/dev/null)" in *builtin*)
    help echo printf >/dev/null
esac
echo ===== 12 $?
case "$(type test 2>/dev/null)" in *builtin*)
    help test [ >/dev/null
esac
echo ===== 13 $?
case "$(type bindkey 2>/dev/null)" in *builtin*)
    help bindkey >/dev/null
esac
echo ===== 14 $?

help --no-such-option
echo help no-such-option $?
help XXX
echo help no-such-builtin $?
(help help help >&- 2>/dev/null)
echo help output error $?
