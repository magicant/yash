# param-y.tst: yash-specific test of parameter expansion

setup -d

test_oE '${#*}'
set 1 22 '3  3'
bracket "${#*}"
__IN__
[9]
__OUT__

test_oE '${#@}'
set 1 22 '3  3'
bracket "${#@}"
__IN__
[1][2][4]
__OUT__

test_oE '$ followed by non-special character'
bracket $% $+ $, $. $/ $: $= $[ $] $^ $} $~
__IN__
[$%][$+][$,][$.][$/][$:][$=][$[][$]][$^][$}][$~]
__OUT__

test_oE '$ followed by space'
bracket $	$ $
__IN__
[$][$][$]
__OUT__

test_oE "\$'"
bracket $'x'
__IN__
[$x]
__OUT__

test_oE '$"'
bracket $"x"
__IN__
[$x]
__OUT__

test_oE '$&'
bracket $&& echo x
__IN__
[$]
x
__OUT__

test_oE '$)'
(bracket $)
__IN__
[$]
__OUT__

test_oE '$;'
bracket $;
__IN__
[$]
__OUT__

test_oE '$<'
bracket $</dev/null
__IN__
[$]
__OUT__

test_oE '$>'
bracket $>&1
__IN__
[$]
__OUT__

test_oE '$\'
bracket $\x
__IN__
[$x]
__OUT__

test_oE '$`'
bracket $`echo x`
__IN__
[$x]
__OUT__

test_oE '$|'
bracket $| cat
__IN__
[$]
__OUT__

test_oE '${##}'
bracket ${##} # length of $#
__IN__
[1]
__OUT__

test_oE '${##x}'
bracket "${##0}" # actually unambiguous, but POSIXly unspecified behavior
__IN__
[]
__OUT__

test_oE '${#%x}'
bracket "${#%0}" # actually unambiguous, but POSIXly unspecified behavior
__IN__
[]
__OUT__

test_oE '${*#x}'
# The matching prefix is removed from each positional parameter and then all
# the parameters are concatenated.
set '1-1-1' 2 '3  -  3'
bracket "${*#*-}"
__IN__
[1-1 2   3]
__OUT__

test_oE '${*%x}'
# The matching suffix is removed from each positional parameter and then all
# the parameters are concatenated.
set '1-1-1' 2 '3  -  3'
bracket "${*%-*}"
__IN__
[1-1 2 3  ]
__OUT__

test_oE '${@#x}'
# The matching suffix is removed from each positional parameter.
set '1-1-1' 2 '3  -  3'
bracket "${@#*-}"
__IN__
[1-1][2][  3]
__OUT__

test_oE '${@%x}'
# The matching suffix is removed from each positional parameter.
set '1-1-1' 2 '3  -  3'
bracket "${@%-*}"
__IN__
[1-1][2][3  ]
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:
