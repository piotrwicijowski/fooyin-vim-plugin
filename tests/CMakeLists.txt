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
    ${CMAKE_SOURCE_DIR}/src/vimmotionsbindingbackend.cpp
    ${CMAKE_SOURCE_DIR}/src/vimmotionssettings.cpp
)
target_include_directories(test_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_integration PRIVATE
    Fooyin::Gui
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

add_executable(test_vimhandler_viewcontext
    vimhandlerviewcontexttest.cpp
    ${CMAKE_SOURCE_DIR}/src/vimactions.cpp
    ${CMAKE_SOURCE_DIR}/src/vimbindingparser.cpp
    ${CMAKE_SOURCE_DIR}/src/vimclipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/vimhandler.cpp
    ${CMAKE_SOURCE_DIR}/src/vimmotionsbindingbackend.cpp
    ${CMAKE_SOURCE_DIR}/src/vimmotionssettings.cpp
    ${CMAKE_SOURCE_DIR}/src/vimsearchbar.cpp
    ${CMAKE_SOURCE_DIR}/src/viewlocator.cpp
    ${CMAKE_SOURCE_DIR}/src/spatialnavigator.cpp
)
target_include_directories(test_vimhandler_viewcontext PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_vimhandler_viewcontext PRIVATE
    Fooyin::Gui
    Qt6::Test
    Qt6::Widgets
    Qt6::Sql
)
add_test(NAME test_vimhandler_viewcontext COMMAND test_vimhandler_viewcontext)

add_executable(test_vimhandler_timeout
    vimhandlertimeouttest.cpp
    ${CMAKE_SOURCE_DIR}/src/vimactions.cpp
    ${CMAKE_SOURCE_DIR}/src/vimbindingparser.cpp
    ${CMAKE_SOURCE_DIR}/src/vimclipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/vimhandler.cpp
    ${CMAKE_SOURCE_DIR}/src/vimmotionsbindingbackend.cpp
    ${CMAKE_SOURCE_DIR}/src/vimmotionssettings.cpp
    ${CMAKE_SOURCE_DIR}/src/vimsearchbar.cpp
    ${CMAKE_SOURCE_DIR}/src/viewlocator.cpp
    ${CMAKE_SOURCE_DIR}/src/spatialnavigator.cpp
)
target_include_directories(test_vimhandler_timeout PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_vimhandler_timeout PRIVATE
    Fooyin::Gui
    Qt6::Test
    Qt6::Widgets
    Qt6::Sql
)
add_test(NAME test_vimhandler_timeout COMMAND test_vimhandler_timeout)
