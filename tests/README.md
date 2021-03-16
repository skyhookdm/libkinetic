# Whitebox Tests for libkinetic 

This directory contains "whitebox" test fixtures, implemented using [Googletest][docs-googletest].
This code relies on Googletest being installed, and currently assumes that the
[Googletest repo][repo-googletest] is checked out as a submodule in `vendor/googletest` and
compiled using `Make`.

This test code is still very early in development, but there is documentation also in the
[repository wiki][wiki-whitebox].


## Test Organization

The organization of tests tries to follow the protocol definition. This hopefully makes it
portable for various implementations, but is primarily done so that test fixtures may be
data-driven and representative of what data must be communicated to the interface and over the
wire. Importantly, these whitebox tests are designed to help test that data is handled correctly
for the lifetime of a kinetic command.

These tests may eventually include performance regressions, but some other aspects of testing such
as memory management are deferred to tools such as `Valgrind` via blackbox tests (written in bash
or using `kctl`).


## Test Fixtures

The following tables are borrowed from the wiki (may be re-formatted in the future), but are
designed to make it easier to understand what fixtures are implemented, and where they can be
found.


### KeyVal Test Fixtures

These are kinetic commands which populate the [KeyVal][protocol-keyval] portion of the protocol.

| Read Only Test Cases              | Status             | Description                                  |
| --------------------------------- | ------------------ | -------------------------------------------- |
| test_get_noexists                 | :white_check_mark: | Get non-existent key                         |
| test_get_exists                   | :white_check_mark: | Get existent key                             |
| test_getversion_noexists          | :pencil:           | Get version of non-existent key              |
| test_getversion_exists            | :pencil:           | Get version of existent key                  |
| test_getnext_noexists             | :pencil:           | Get key proceeding the last key              |
| test_getnext_exists               | :pencil:           | Get key proceeding any key before the last   |
| test_getprev_noexists             | :pencil:           | Get key preceeding the first key             |
| test_getprev_exists               | :pencil:           | Get key preceeding any key after the first   |


| Modification Test Cases           | Status             | Description                                             |
| --------------------------------- | ------------------ | ------------------------------------------------------- |
| test_putkey_getkey_insertcheckdel | :white_check_mark: | Insert a key, get the key, delete the key               |
| test_putkey_doesnotexist          | :o:                | Insert a non-existing key, with force                   |
| test_delkey_doesnotexist          | :o:                | Delete a non-existing key, with force                   |
| test_putkey_exists                | :o:                | Insert an existing key, with force                      |
| test_delkey_exists                | :o:                | Delete an existing key, with force                      |
| test_putkey_cas_doesnotexist      | :o:                | Insert a non-existing key, no force                     |
| test_delkey_cad_doesnotexist      | :o:                | Delete a non-existing key, no force                     |
| test_putkey_cas_exists_noconflict | :o:                | Insert an existing key, no force, correct dbversion     |
| test_putkey_cas_exists_conflict   | :o:                | Insert an existing key, no force, conflicting dbversion |
| test_delkey_cad_exists_noconflict | :o:                | Delete an existing key, no force, correct dbversion     |
| test_delkey_cad_exists_conflict   | :o:                | Delete an existing key, no force, conflicting dbversion |


<!-- Resources -->
[repo-googletest]: https://github.com/google/googletest
[docs-googletest]: https://google.github.io/googletest/

[wiki-whitebox]:   https://gitlab.com/kinetic-storage/libkinetic/-/wikis/WhiteBox-Tests

[protocol-keyval]: https://github.com/Kinetic/kinetic-protocol/blob/master/kinetic.proto#L311
