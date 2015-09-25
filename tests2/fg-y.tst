# fg-y.tst: yash-specific test of the fg built-in
../checkfg || skip="true" # %SEQUENTIAL%

# POSIX requires that "fg" should return 0 on success. Yash, however, returns
# the exit status of the resumed job, which is not always 0. Many other shells
# behave this way.
test_x -e 17 'exit status of resumed command' -m
sh -c 'kill -s STOP $$; exit 17'
fg
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:
