find_package(Qt6 REQUIRED COMPONENTS Test)

# Document is compiled into the test: the app is an executable, not a library.
qt_add_executable(tst_document
    tst_document.h
    tst_document.cpp
    ../src/Document.h
    ../src/Document.cpp
)
target_include_directories(tst_document PRIVATE ../src)
target_link_libraries(tst_document PRIVATE Qt6::Quick Qt6::Test)
add_test(NAME tst_document COMMAND tst_document)
set_tests_properties(tst_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_textbuffer
    tst_textbuffer.h
    tst_textbuffer.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
)
target_include_directories(tst_textbuffer PRIVATE ../src/editor)
target_link_libraries(tst_textbuffer PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_textbuffer COMMAND tst_textbuffer)
