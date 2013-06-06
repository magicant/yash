# prompt.y.tst: test of interactive prompt
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

$INVOKE $TESTEE -iv +m -o posix <<\END
printf 'posix PS1={%s}\n' "$PS1"
printf 'posix PS2={%s}\n' "$PS2"
printf 'posix PS4={%s}\n' "$PS4"
exit
END
$INVOKE $TESTEE -iv +m --norcfile <<\END
printf 'yash PS1={%s}\n' "$PS1"
printf 'yash PS2={%s}\n' "$PS2"
printf 'yash PS4={%s}\n' "$PS4"
exit
END
