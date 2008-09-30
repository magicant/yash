PS1= PS2=; echo >&2 #1

echo 2 #2
history #3
echo 4 #4
history 2 #5
history 0 #6
history -c #7
history #1 again
echo 2 #2
history -w "$HISTFILE" #3
history -r #4
history -s 'echo #8'
history #9
history -a #10
echo 11 #11
history -a #12
echo =====
cat "$HISTFILE"
