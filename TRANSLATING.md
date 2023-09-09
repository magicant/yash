# Translating

## Translating messages printed by yash

Yash uses [GNU gettext] to localize printed messages.

To add translation for a new language:

1. Fork and clone this repository.
2. Install make and gettext.
3. Run `./configure`.
4. Enter the `po` directory and run `make`.
5. Run `msginit`. This command creates a new po file with a name corresponding to your current locale.
6. Edit the po file adding translation for each message.
7. Commit the file, push it to GitHub, and make a pull request.

To update existing translation, follow the steps above except that
you edit the existing po file instead of creating a new one with `msginit`.
Outdated translations are marked as `fuzzy` by the gettext tool.
Remove the `fuzzy` mark and correct the translation.

## Translating the manual

The [manual](doc) is written in the asciidoc format and converted to HTML and manpages by [asciidoc].
Preparing the manual for a new translation is not very automated:
A few files have to be copied by hand.
Possibly you might have to localize asciidoc before translating the manual.
Please let me know if you are interested in translating the manual.

[GNU gettext]: https://www.gnu.org/software/gettext/
[asciidoc]: https://asciidoc.org/
