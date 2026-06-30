# Register this module's unit tests into the shared `unit_tests` target.
set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/MulticlassLogicTest.cpp")

set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
