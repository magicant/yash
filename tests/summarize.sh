# summarize.sh: extracts results of failed tests
# (C) 2015-2020 magicant
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

set -Ceu

export LC_ALL=C

uname -a
date
printf '=============\n\n'

passed=0 failed=0 skipped=0

for result_file do
    # The "grep" command is generally faster than repeated "read" built-in.
    if [ "$(grep -cE '^%%% (FAIL|SKIPP)ED:' "$result_file")" -eq 0 ]; then
	passed="$((passed + $(grep -c '^%%% PASSED:' "$result_file" || true)))"
	continue
    fi

    log=''
    while IFS= read -r line; do
	log="$log
$line"
	case $line in
	    ('%%% START:'*)
		log="$line"
		;;
	    ('%%% PASSED:'*)
		passed="$((passed + 1))"
		;;
	    ('%%% FAILED:'*)
		printf '%s\n\n' "$log"
		failed="$((failed + 1))"
		;;
	    ('%%% SKIPPED:'*)
		printf '%s\n\n' "$line"
		skipped="$((skipped + 1))"
		;;
	esac
    done <"$result_file"
done

printf '==============\n'
printf 'TOTAL:   %5d\n' "$((passed + failed + skipped))"
printf 'PASSED:  %5d\n' "$passed"
printf 'FAILED:  %5d\n' "$failed"
printf 'SKIPPED: %5d\n' "$skipped"
printf '==============\n'

# vim: set ts=8 sts=4 sw=4 noet:
