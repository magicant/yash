# This script prints a section list for index.txt.
# (C) 2012 magicant
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

# skip up to the "//*//" marker
while read -r line; do
    case $line in
        //*//)
            break;;
        *)
            printf '%s\n' "$line";;
    esac
done

# insert include macros
printf 'include::%s[]\n\n' "$@"

# print the rest of the input as is
exec cat

# vim: set ft=sh ts=8 sts=4 sw=4 et tw=80:
