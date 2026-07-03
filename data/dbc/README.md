# DBC overrides

These `.dbc` files are overrides. On `make install` they are deployed into the world
server's runtime `dbc/` -- the effective `DataDir` (`bin/dbc` by default, or `$AC_DATA_DIR/dbc`
when that is set); see the install rule in `src/server/apps/CMakeLists.txt` -- replacing the
extractor-produced copies.

The **client-side** copies of these overrides live in
`client/modified/DBFilesClient/` and must byte-mirror what ships in the patch MPQ;
the server gates content per character, so no cross-class content leaks.
