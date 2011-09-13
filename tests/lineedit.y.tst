# lineedit.y.tst: yash-specific test of the bindkey/complete builtin
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp=$TESTTMP/lineedit.y.tmp
mkdir "$tmp"

echo ===== bindkey =====

bindkey -l | sort >"$tmp/l"
sort <<\END | diff - "$tmp/l"
noop
alert
self-insert
insert-tab
expect-verbatim
digit-argument
bol-or-digit
accept-line
abort-line
eof
eof-if-empty
eof-or-delete
accept-with-hash
setmode-viinsert
setmode-vicommand
setmode-emacs
expect-char
abort-expect-char
redraw-all
clear-and-redraw-all
forward-char
backward-char
forward-bigword
end-of-bigword
backward-bigword
forward-semiword
end-of-semiword
backward-semiword
forward-viword
end-of-viword
backward-viword
forward-emacsword
backward-emacsword
beginning-of-line
end-of-line
go-to-column
first-nonblank
find-char
find-char-rev
till-char
till-char-rev
refind-char
refind-char-rev
delete-char
delete-bigword
delete-semiword
delete-viword
delete-emacsword
backward-delete-char
backward-delete-bigword
backward-delete-semiword
backward-delete-viword
backward-delete-emacsword
delete-line
forward-delete-line
backward-delete-line
kill-char
kill-bigword
kill-semiword
kill-viword
kill-emacsword
backward-kill-char
backward-kill-bigword
backward-kill-semiword
backward-kill-viword
backward-kill-emacsword
kill-line
forward-kill-line
backward-kill-line
put-before
put
put-left
put-pop
undo
undo-all
cancel-undo
cancel-undo-all
redo
complete
complete-next-candidate
complete-prev-candidate
complete-next-column
complete-prev-column
complete-next-page
complete-prev-page
complete-list
complete-all
complete-max
clear-candidates
vi-replace-char
vi-insert-beginning
vi-append
vi-append-to-eol
vi-replace
vi-switch-case
vi-switch-case-char
vi-yank
vi-yank-to-eol
vi-delete
vi-delete-to-eol
vi-change
vi-change-to-eol
vi-change-line
vi-yank-and-change
vi-yank-and-change-to-eol
vi-yank-and-change-line
vi-substitute
vi-append-last-bigword
vi-exec-alias
vi-edit-and-accept
vi-complete-list
vi-complete-all
vi-complete-max
vi-search-forward
vi-search-backward
emacs-transpose-chars
emacs-transpose-words
emacs-upcase-word
emacs-downcase-word
emacs-capitalize-word
emacs-delete-horizontal-space
emacs-just-one-space
emacs-search-forward
emacs-search-backward
oldest-history
newest-history
return-history
oldest-history-bol
newest-history-bol
return-history-bol
oldest-history-eol
newest-history-eol
return-history-eol
next-history
prev-history
next-history-bol
prev-history-bol
next-history-eol
prev-history-eol
srch-self-insert
srch-backward-delete-char
srch-backward-delete-line
srch-continue-forward
srch-continue-backward
srch-accept-search
srch-abort-search
search-again
search-again-rev
search-again-forward
search-again-backward
beginning-search-forward
beginning-search-backward
END

bindkey -v | sort >"$tmp/v"
sort <<\END | diff - "$tmp/v"
bindkey -v '\\' self-insert
bindkey -v '\^V' expect-verbatim
bindkey -v '\^J' accept-line
bindkey -v '\^M' accept-line
bindkey -v '\!' abort-line
bindkey -v '\^C' abort-line
bindkey -v '\#' eof-if-empty
bindkey -v '\^D' eof-if-empty
bindkey -v '\^[' setmode-vicommand
bindkey -v '\^L' redraw-all
bindkey -v '\R' forward-char
bindkey -v '\L' backward-char
bindkey -v '\H' beginning-of-line
bindkey -v '\E' end-of-line
bindkey -v '\X' delete-char
bindkey -v '\B' backward-delete-char
bindkey -v '\?' backward-delete-char
bindkey -v '\^H' backward-delete-char
bindkey -v '\^W' backward-delete-semiword
bindkey -v '\$' backward-delete-line
bindkey -v '\^U' backward-delete-line
bindkey -v '\D' next-history-eol
bindkey -v '\^N' next-history-eol
bindkey -v '\U' prev-history-eol
bindkey -v '\^P' prev-history-eol
bindkey -v '\^I' complete-next-candidate
bindkey -v '\bt' complete-prev-candidate
END
bindkey -v '\#'
bindkey -v 'a' self-insert
bindkey -v 'a'
bindkey -v '\#' noop
bindkey -v '\#'
bindkey -v 'a' -
bindkey -v 'a' 2>/dev/null; echo unbound 1 $?
bindkey -v '\#' -
bindkey -v '\#' 2>/dev/null; echo unbound 2 $?
bindkey -v '\X' -
bindkey -v '\X' 2>/dev/null; echo unbound 3 $?
bindkey -v 'a' noop
bindkey -v '\#' eof
bindkey -v '\X' emacs-transpose-chars
bindkey -v 'a'
bindkey -v '\#'
bindkey -v '\X'
bindkey -v 'a' -
bindkey -v '\\' -
bindkey -v '\^V' -
bindkey -v '\^J' -
bindkey -v '\^M' -
bindkey -v '\!' -
bindkey -v '\^C' -
bindkey -v '\#' -
bindkey -v '\^D' -
bindkey -v '\^[' -
bindkey -v '\^L' -
bindkey -v '\R' -
bindkey -v '\L' -
bindkey -v '\H' -
bindkey -v '\E' -
bindkey -v '\X' -
bindkey -v '\B' -
bindkey -v '\?' -
bindkey -v '\^H' -
bindkey -v '\^W' -
bindkey -v '\$' -
bindkey -v '\^U' -
bindkey -v '\D' -
bindkey -v '\^N' -
bindkey -v '\U' -
bindkey -v '\^P' -
bindkey -v '\^I' -
bindkey -v '\bt' -
bindkey -v
echo no bindings $?
. "$tmp/v"
bindkey --vi-insert | sort | diff - "$tmp/v"

bindkey -a | sort >"$tmp/a"
sort <<\END | diff - "$tmp/a"
bindkey -a '\^[' noop
bindkey -a '1' digit-argument
bindkey -a '2' digit-argument
bindkey -a '3' digit-argument
bindkey -a '4' digit-argument
bindkey -a '5' digit-argument
bindkey -a '6' digit-argument
bindkey -a '7' digit-argument
bindkey -a '8' digit-argument
bindkey -a '9' digit-argument
bindkey -a '0' bol-or-digit
bindkey -a '\^J' accept-line
bindkey -a '\^M' accept-line
bindkey -a '\!' abort-line
bindkey -a '\^C' abort-line
bindkey -a '\#' eof-if-empty
bindkey -a '\^D' eof-if-empty
bindkey -a '#' accept-with-hash
bindkey -a 'i' setmode-viinsert
bindkey -a '\I' setmode-viinsert
bindkey -a '\^L' redraw-all
bindkey -a 'l' forward-char
bindkey -a ' ' forward-char
bindkey -a '\R' forward-char
bindkey -a 'h' backward-char
bindkey -a '\L' backward-char
bindkey -a '\B' backward-char
bindkey -a '\?' backward-char
bindkey -a '\^H' backward-char
bindkey -a 'W' forward-bigword
bindkey -a 'E' end-of-bigword
bindkey -a 'B' backward-bigword
bindkey -a 'w' forward-viword
bindkey -a 'e' end-of-viword
bindkey -a 'b' backward-viword
bindkey -a '\H' beginning-of-line
bindkey -a '$' end-of-line
bindkey -a '\E' end-of-line
bindkey -a '|' go-to-column
bindkey -a '^' first-nonblank
bindkey -a 'f' find-char
bindkey -a 'F' find-char-rev
bindkey -a 't' till-char
bindkey -a 'T' till-char-rev
bindkey -a ';' refind-char
bindkey -a ',' refind-char-rev
bindkey -a 'x' kill-char
bindkey -a '\X' kill-char
bindkey -a 'X' backward-kill-char
bindkey -a 'P' put-before
bindkey -a 'p' put
bindkey -a 'u' undo
bindkey -a 'U' undo-all
bindkey -a '\^R' cancel-undo
bindkey -a '.' redo
bindkey -a 'r' vi-replace-char
bindkey -a 'I' vi-insert-beginning
bindkey -a 'a' vi-append
bindkey -a 'A' vi-append-to-eol
bindkey -a 'R' vi-replace
bindkey -a '~' vi-switch-case-char
bindkey -a 'y' vi-yank
bindkey -a 'Y' vi-yank-to-eol
bindkey -a 'd' vi-delete
bindkey -a 'D' forward-kill-line
bindkey -a 'c' vi-change
bindkey -a 'C' vi-change-to-eol
bindkey -a 'S' vi-change-line
bindkey -a 's' vi-substitute
bindkey -a '_' vi-append-last-bigword
bindkey -a '@' vi-exec-alias
bindkey -a 'v' vi-edit-and-accept
bindkey -a '=' vi-complete-list
bindkey -a '*' vi-complete-all
bindkey -a '\\' vi-complete-max
bindkey -a '?' vi-search-forward
bindkey -a '/' vi-search-backward
bindkey -a 'G' oldest-history-bol
bindkey -a 'g' return-history-bol
bindkey -a 'j' next-history-bol
bindkey -a '+' next-history-bol
bindkey -a '\D' next-history-bol
bindkey -a '\^N' next-history-bol
bindkey -a 'k' prev-history-bol
bindkey -a -- '-' prev-history-bol
bindkey -a '\U' prev-history-bol
bindkey -a '\^P' prev-history-bol
bindkey -a 'n' search-again
bindkey -a 'N' search-again-rev
END
. "$tmp/a"
bindkey --vi-command | sort | diff - "$tmp/a"

bindkey -e | sort >"$tmp/e"
sort <<\END | diff - "$tmp/e"
bindkey -e '\\' self-insert
bindkey -e '\^[\^I' insert-tab
bindkey -e '\^Q' expect-verbatim
bindkey -e '\^V' expect-verbatim
bindkey -e '\^[0' digit-argument
bindkey -e '\^[1' digit-argument
bindkey -e '\^[2' digit-argument
bindkey -e '\^[3' digit-argument
bindkey -e '\^[4' digit-argument
bindkey -e '\^[5' digit-argument
bindkey -e '\^[6' digit-argument
bindkey -e '\^[7' digit-argument
bindkey -e '\^[8' digit-argument
bindkey -e '\^[9' digit-argument
bindkey -e '\^[-' digit-argument
bindkey -e '\^J' accept-line
bindkey -e '\^M' accept-line
bindkey -e '\!' abort-line
bindkey -e '\^C' abort-line
bindkey -e '\#' eof-or-delete
bindkey -e '\^D' eof-or-delete
bindkey -e '\^[#' accept-with-hash
bindkey -e '\^L' redraw-all
bindkey -e '\R' forward-char
bindkey -e '\^F' forward-char
bindkey -e '\L' backward-char
bindkey -e '\^B' backward-char
bindkey -e '\^[f' forward-emacsword
bindkey -e '\^[F' forward-emacsword
bindkey -e '\^[b' backward-emacsword
bindkey -e '\^[B' backward-emacsword
bindkey -e '\H' beginning-of-line
bindkey -e '\^A' beginning-of-line
bindkey -e '\E' end-of-line
bindkey -e '\^E' end-of-line
bindkey -e '\^]' find-char
bindkey -e '\^[\^]' find-char-rev
bindkey -e '\X' delete-char
bindkey -e '\B' backward-delete-char
bindkey -e '\?' backward-delete-char
bindkey -e '\^H' backward-delete-char
bindkey -e '\^[d' kill-emacsword
bindkey -e '\^[D' kill-emacsword
bindkey -e '\^W' backward-kill-bigword
bindkey -e '\^[\B' backward-kill-emacsword
bindkey -e '\^[\?' backward-kill-emacsword
bindkey -e '\^[\^H' backward-kill-emacsword
bindkey -e '\^K' forward-kill-line
bindkey -e '\$' backward-kill-line
bindkey -e '\^U' backward-kill-line
bindkey -e '\^X\B' backward-kill-line
bindkey -e '\^X\?' backward-kill-line
bindkey -e '\^Y' put-left
bindkey -e '\^[y' put-pop
bindkey -e '\^[Y' put-pop
bindkey -e '\^_' undo
bindkey -e '\^X\$' undo
bindkey -e '\^X\^U' undo
bindkey -e '\^[\^R' undo-all
bindkey -e '\^[r' undo-all
bindkey -e '\^[R' undo-all
bindkey -e '\^I' complete-next-candidate
bindkey -e '\bt' complete-prev-candidate
bindkey -e '\^[=' complete-list
bindkey -e '\^[?' complete-list
bindkey -e '\^[*' complete-all
bindkey -e '\^T' emacs-transpose-chars
bindkey -e '\^[t' emacs-transpose-words
bindkey -e '\^[T' emacs-transpose-words
bindkey -e '\^[l' emacs-downcase-word
bindkey -e '\^[L' emacs-downcase-word
bindkey -e '\^[u' emacs-upcase-word
bindkey -e '\^[U' emacs-upcase-word
bindkey -e '\^[c' emacs-capitalize-word
bindkey -e '\^[C' emacs-capitalize-word
bindkey -e '\^[\\' emacs-delete-horizontal-space
bindkey -e '\^[ ' emacs-just-one-space
bindkey -e '\^S' emacs-search-forward
bindkey -e '\^R' emacs-search-backward
bindkey -e '\^[<' oldest-history-eol
bindkey -e '\^[>' return-history-eol
bindkey -e '\D' next-history-eol
bindkey -e '\^N' next-history-eol
bindkey -e '\U' prev-history-eol
bindkey -e '\^P' prev-history-eol
END
. "$tmp/e"
bindkey --emacs | sort | diff - "$tmp/e"

bindkey -l >/dev/null; echo 1 $?
bindkey -v >/dev/null; echo 2 $?
bindkey -v '\^[' >/dev/null; echo 3 $?
bindkey -v '\^[' setmode-viinsert; echo 4 $?

for cmd in $(bindkey -l)
do
    bindkey -v '!!!' $cmd || echo bindkey -v !!! $cmd $?
    bindkey -a '!!!' $cmd || echo bindkey -a !!! $cmd $?
    bindkey -e '!!!' $cmd || echo bindkey -e !!! $cmd $?
done
bindkey -v '!!!' -
bindkey -a '!!!' -
bindkey -e '!!!' -

rm -fr "$tmp"


echo ===== error =====

bindkey --no-such-option
echo bindkey no-such-option $?
bindkey
echo bindkey operand missing $?
bindkey -v x y z
echo bindkey too many operands $?
bindkey -v '\\' no-such-command
echo bindkey invalid operand 1 $?
bindkey -v '' abort-line
echo bindkey invalid operand 2 $?
bindkey -v '~~~'
echo bindkey unbound sequence $?
(bindkey -l >&- 2>/dev/null)
echo bindkey output error 1 $?
(bindkey -v >&- 2>/dev/null)
echo bindkey output error 2 $?

complete --no-such-option
echo complete no-such-option $?
complete
echo complete not-completing $?
