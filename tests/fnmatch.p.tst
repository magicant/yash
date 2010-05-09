# fnmatch.p.tst: test of pattern matching for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

case a in a) echo 1; esac
case a in b) echo 2; esac
case a in A) echo 3; esac
case \a in a) echo 11; esac
case \a in b) echo 12; esac
case \a in A) echo 13; esac
case a in \a) echo 21; esac
case a in \b) echo 22; esac
case a in \A) echo 23; esac
case \a in \a) echo 31; esac
case \a in \b) echo 32; esac
case \a in \A) echo 33; esac
case 'a' in a) echo 41; esac
case 'a' in b) echo 42; esac
case 'a' in A) echo 43; esac
case a in 'a') echo 51; esac
case a in 'b') echo 52; esac
case a in 'A') echo 53; esac
case 'a' in 'a') echo 61; esac
case 'a' in 'b') echo 62; esac
case 'a' in 'A') echo 63; esac
case "a" in a) echo 71; esac
case "a" in b) echo 72; esac
case "a" in A) echo 73; esac
case a in "a") echo 81; esac
case a in "b") echo 82; esac
case a in "A") echo 83; esac
case "a" in "a") echo 91; esac
case "a" in "b") echo 92; esac
case "a" in "A") echo 93; esac

sq=\' dq=\" bs=\\
case \'    in \'   ) echo 111; esac
case \'    in "'"  ) echo 112; esac
case \'    in $sq  ) echo 113; esac
case \'    in "$sq") echo 114; esac
case "'"   in \'   ) echo 121; esac
case "'"   in "'"  ) echo 122; esac
case "'"   in $sq  ) echo 123; esac
case "'"   in "$sq") echo 124; esac
case $sq   in \'   ) echo 131; esac
case $sq   in "'"  ) echo 132; esac
case $sq   in $sq  ) echo 133; esac
case $sq   in "$sq") echo 134; esac
case "$sq" in \'   ) echo 141; esac
case "$sq" in "'"  ) echo 142; esac
case "$sq" in $sq  ) echo 143; esac
case "$sq" in "$sq") echo 144; esac
case \"    in \"   ) echo 211; esac
case \"    in '"'  ) echo 212; esac
case \"    in $dq  ) echo 213; esac
case \"    in "$dq") echo 214; esac
case '"'   in \"   ) echo 221; esac
case '"'   in '"'  ) echo 222; esac
case '"'   in $dq  ) echo 223; esac
case '"'   in "$dq") echo 224; esac
case $dq   in \"   ) echo 231; esac
case $dq   in '"'  ) echo 232; esac
case $dq   in $dq  ) echo 233; esac
case $dq   in "$dq") echo 234; esac
case "$dq" in \"   ) echo 241; esac
case "$dq" in '"'  ) echo 242; esac
case "$dq" in $dq  ) echo 243; esac
case "$dq" in "$dq") echo 244; esac
case \\    in \\   ) echo 311; esac
case \\    in '\'  ) echo 312; esac
case \\    in "\\" ) echo 313; esac
case \\    in $bs  ) echo 314; esac
case \\    in "$bs") echo 315; esac
case '\'   in \\   ) echo 321; esac
case '\'   in '\'  ) echo 322; esac
case '\'   in "\\" ) echo 323; esac
case '\'   in $bs  ) echo 324; esac
case '\'   in "$bs") echo 325; esac
case "\\"  in \\   ) echo 331; esac
case "\\"  in '\'  ) echo 332; esac
case "\\"  in "\\" ) echo 333; esac
case "\\"  in $bs  ) echo 334; esac
case "\\"  in "$bs") echo 335; esac
case $bs   in \\   ) echo 341; esac
case $bs   in '\'  ) echo 342; esac
case $bs   in "\\" ) echo 343; esac
case $bs   in $bs  ) echo 344; esac
case $bs   in "$bs") echo 345; esac
case "$bs" in \\   ) echo 351; esac
case "$bs" in '\'  ) echo 352; esac
case "$bs" in "\\" ) echo 353; esac
case "$bs" in $bs  ) echo 354; esac
case "$bs" in "$bs") echo 355; esac
case \'"'"$sq"$sq" in "$sq"$sq"'"\') echo 391; esac
case \"'"'$dq"$dq" in "$dq"$dq'"'\") echo 392; esac
case \\'\'"\\"$bs"$bs" in "$bs"$bs"\\"'\'\\) echo 393; esac

n=
case ''   in ''  ) echo 1011; esac
case ''   in ""  ) echo 1012; esac
case ''   in $n  ) echo 1013; esac
case ''   in "$n") echo 1014; esac
case ""   in ''  ) echo 1021; esac
case ""   in ""  ) echo 1022; esac
case ""   in $n  ) echo 1023; esac
case ""   in "$n") echo 1024; esac
case $n   in ''  ) echo 1031; esac
case $n   in ""  ) echo 1032; esac
case $n   in $n  ) echo 1033; esac
case $n   in "$n") echo 1034; esac
case "$n" in ''  ) echo 1041; esac
case "$n" in ""  ) echo 1042; esac
case "$n" in $n  ) echo 1043; esac
case "$n" in "$n") echo 1044; esac
case $n''"""$n" in "$n"""''$n) echo 1099; esac

case a  in a ) echo 2001; esac
case aa in a ) echo 2002; esac
case a  in aa) echo 2003; esac
case aa in aa) echo 2004; esac
case a  in ? ) echo 2011; esac
case a  in * ) echo 2012; esac
case a  in ?*) echo 2013; esac
case a  in *?) echo 2014; esac
case a  in ??) echo 2015; esac
case a  in **) echo 2016; esac
case aa in ? ) echo 2021; esac
case aa in * ) echo 2022; esac
case aa in ?*) echo 2023; esac
case aa in *?) echo 2024; esac
case aa in ??) echo 2025; esac
case aa in **) echo 2026; esac

case ''  in ?) echo 2101; esac
case ''  in *) echo 2102; esac
case \\  in ?) echo 2111; esac
case \\  in *) echo 2112; esac
case "'" in ?) echo 2121; esac
case "'" in *) echo 2122; esac
case '"' in ?) echo 2131; esac
case '"' in *) echo 2132; esac

case a in [[:lower:]])  echo 3001 lower ; esac
case a in [[:upper:]])  echo 3001 upper ; esac
case a in [[:alpha:]])  echo 3001 alpha ; esac
case a in [[:digit:]])  echo 3001 digit ; esac
case a in [[:alnum:]])  echo 3001 alnum ; esac
case a in [[:punct:]])  echo 3001 punct ; esac
case a in [[:graph:]])  echo 3001 graph ; esac
case a in [[:print:]])  echo 3001 print ; esac
case a in [[:cntrl:]])  echo 3001 cntrl ; esac
case a in [[:blank:]])  echo 3001 blank ; esac
case a in [[:space:]])  echo 3001 space ; esac
case a in [[:xdigit:]]) echo 3001 xdigit; esac
case a in [[.a.]]      ) echo 3002; esac
case 1 in [0-2]        ) echo 3003; esac
case 1 in [[.0.]-[.2.]]) echo 3004; esac
case a in [!a]         ) echo 3005; esac
case 1 in [!0-2]       ) echo 3006; esac
case a in [[=a=]]      ) echo 3007; esac

case \. in ["."]) echo 3101; esac
case \[ in ["."]) echo 3102; esac
case \" in ["."]) echo 3103; esac
case \\ in ["."]) echo 3104; esac
case \] in ["."]) echo 3105; esac
case \. in [\".]) echo 3111; esac
case \[ in [\".]) echo 3112; esac
case \" in [\".]) echo 3113; esac
case \\ in [\".]) echo 3114; esac
case \] in [\".]) echo 3115; esac
case \. in [\.] ) echo 3121; esac
case \[ in [\.] ) echo 3122; esac
case \" in [\.] ) echo 3123; esac
case \\ in [\.] ) echo 3124; esac
case \] in [\.] ) echo 3125; esac
case \. in "[.]") echo 3131; esac
case \[ in "[.]") echo 3132; esac
case \" in "[.]") echo 3133; esac
case \\ in "[.]") echo 3134; esac
case \] in "[.]") echo 3135; esac
case \. in [\]] ) echo 3141; esac
case \[ in [\]] ) echo 3142; esac
case \" in [\]] ) echo 3143; esac
case \\ in [\]] ) echo 3144; esac
case \] in [\]] ) echo 3145; esac
case \. in ["]"]) echo 3151; esac
case \[ in ["]"]) echo 3152; esac
case \" in ["]"]) echo 3153; esac
case \\ in ["]"]) echo 3154; esac
case \] in ["]"]) echo 3155; esac
