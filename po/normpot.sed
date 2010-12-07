# This sed script removes the POT-Creation-Date line in the header entry
# from a POT file. It also replaces some placeholder strings.

/^"POT-Creation-Date: .*"$/ d
