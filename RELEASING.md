# Yash release procedure

1. Run `make update-po` in [po] and see if there is any text that needs
   translation. If all the texts are up-to-date, revert (`git reset`) the
   change in auto- generated comments to avoid committing it.
1. Make sure the latest commit (which is to be released) has been successfully
   tested by Jenkins:
    - [Full test]
    - [Valgrind test]
    - [Extra test]
    - Note that these tests are run no more than once a day.
      You may have to wait for the tests to complete.
1. Update the version number in [configure].
    - See commit [b83905d] for an example of what to change.
    - Also update the year in the copyright notice if releasing for the first
      time in the current year.
1. Update [NEWS] and [NEWS.ja] to include the release date.
1. Commit and push the change.
1. Obtain the distribution tarball from Jenkins. ([Distribution build])
    - Make sure the tarball includes the just committed update.
1. Tag and push the revision.
1. Release the tarball on GitHub. ([Releases])
1. Update [doc] on the gh-pages branch.
    - All the files should be copied intact from the [Documentation build] based
      on the [Distribution build] for the released commit.

[b83905d]: https://github.com/magicant/yash/commit/b83905d458bb858855360bc5cfb610a8d0c14af6
[configure]: configure
[Distribution build]: https://jenkins.wonderwand.net/job/yash/job/yash_dist/
[doc]: https://github.com/magicant/yash/tree/gh-pages/doc
[Documentation build]: https://jenkins.wonderwand.net/job/yash/job/yash_doc/
[Extra test]: https://jenkins.wonderwand.net/job/yash/job/yash_extra/
[Full test]: https://jenkins.wonderwand.net/job/yash/job/yash_fulltest/
[NEWS]: NEWS
[NEWS.ja]: NEWS.ja
[po]: po
[Releases]: https://github.com/magicant/yash/releases
[Valgrind test]: https://jenkins.wonderwand.net/job/yash/job/yash_valgrind/
