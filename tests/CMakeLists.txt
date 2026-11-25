find_package(Qt6 REQUIRED COMPONENTS Test)

# The sources under test are compiled into each test: the app is an
# executable, not a library.
set(editor_sources
    ../src/editor/EditorAccessible.h
    ../src/editor/EditorAccessible.cpp
    ../src/editor/EditorView.h
    ../src/editor/EditorView.cpp
    ../src/editor/SyntaxHighlighter.h
    ../src/editor/SyntaxHighlighter.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
)

qt_add_executable(tst_document
    tst_document.h
    tst_document.cpp
    ../src/Document.h
    ../src/Document.cpp
    ${editor_sources}
)
target_include_directories(tst_document PRIVATE ../src ../src/editor)
target_link_libraries(tst_document PRIVATE Qt6::Quick Qt6::Test KF6::SyntaxHighlighting)
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

qt_add_executable(tst_editorview
    tst_editorview.h
    tst_editorview.cpp
    ${editor_sources}
)
target_include_directories(tst_editorview PRIVATE ../src/editor)
target_link_libraries(tst_editorview PRIVATE Qt6::Quick Qt6::Test KF6::SyntaxHighlighting)
add_test(NAME tst_editorview COMMAND tst_editorview)
set_tests_properties(tst_editorview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_syntaxhighlighter
    tst_syntaxhighlighter.h
    tst_syntaxhighlighter.cpp
    ../src/editor/SyntaxHighlighter.h
    ../src/editor/SyntaxHighlighter.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
)
target_include_directories(tst_syntaxhighlighter PRIVATE ../src/editor)
target_link_libraries(tst_syntaxhighlighter PRIVATE Qt6::Gui Qt6::Test KF6::SyntaxHighlighting)
add_test(NAME tst_syntaxhighlighter COMMAND tst_syntaxhighlighter)
set_tests_properties(tst_syntaxhighlighter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
