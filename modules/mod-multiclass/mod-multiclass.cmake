# Deploy this module's custom DBC overrides into the server's runtime dbc/ on `make install`.
# AzerothCore has no generic module-data install, so this mirrors how mod-eluna ships its
# lua_scripts (see modules/CMakeLists.txt). Every *.dbc under data/dbc/ is copied over
# ${prefix}/bin/dbc; FILES_MATCHING keeps stray non-DBC files from being deployed.
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/data/dbc/"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/dbc"
        FILES_MATCHING PATTERN "*.dbc")
