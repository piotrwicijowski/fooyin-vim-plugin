# Parse-binding unit tests (no QApplication needed)
add_executable(test_parsebinding
    bindingparsertest.cpp
    ${CMAKE_SOURCE_DIR}/src/vimbindingparser.cpp
)
target_include_directories(test_parsebinding PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_parsebinding PRIVATE
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME test_parsebinding COMMAND test_parsebinding)

# Integration test: config file read + parser
add_executable(test_integration
    bindingintegrationtest.cpp
    ${CMAKE_SOURCE_DIR}/src/vimbindingparser.cpp
)
target_include_directories(test_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_integration PRIVATE
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME test_integration COMMAND test_integration)

add_executable(test_searchnavigation
    searchnavigationtest.cpp
)
target_include_directories(test_searchnavigation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_searchnavigation PRIVATE
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME test_searchnavigation COMMAND test_searchnavigation)
