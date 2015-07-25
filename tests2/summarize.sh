# summarize.sh: extracts results of failed tests
# (C) 2015 magicant
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

passed=0 failed=0

for result_file do
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
	esac
    done <"$result_file"
done

printf '============\n'
printf 'TOTAL:  %4d\n' "$((passed + failed))"
printf 'PASSED: %4d\n' "$passed"
printf 'FAILED: %4d\n' "$failed"
printf '============\n'

# vim: set ts=8 sts=4 sw=4 noet:
