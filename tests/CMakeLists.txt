find_package(Qt6 REQUIRED COMPONENTS Test)

# The sources under test are compiled into each test: the app is an
# executable, not a library.
set(editor_sources
    ../src/editor/Commenting.h
    ../src/editor/Commenting.cpp
    ../src/editor/EditorAccessible.h
    ../src/editor/EditorAccessible.cpp
    ../src/editor/EditorView.h
    ../src/editor/EditorView.cpp
    ../src/editor/Folding.h
    ../src/editor/Folding.cpp
    ../src/editor/Indentation.h
    ../src/editor/Indentation.cpp
    ../src/editor/Matching.h
    ../src/editor/Matching.cpp
    ../src/editor/Minimap.h
    ../src/editor/Minimap.cpp
    ../src/editor/SyntaxHighlighter.h
    ../src/editor/SyntaxHighlighter.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
    ../src/editor/TextDocument.h
    ../src/editor/TextDocument.cpp
    ../src/editor/TextSearch.h
    ../src/editor/TextSearch.cpp
    ../src/editor/WrapIndex.h
    ../src/editor/WrapIndex.cpp
)

set(lsp_sources
    ../src/lsp/LanguageServer.h
    ../src/lsp/LanguageServer.cpp
    ../src/lsp/LanguageServers.h
    ../src/lsp/LanguageServers.cpp
    ../src/lsp/Protocol.h
    ../src/lsp/Protocol.cpp
    ../src/lsp/SemanticTokens.h
    ../src/lsp/SemanticTokens.cpp
)

qt_add_executable(tst_document
    tst_document.h
    tst_document.cpp
    ../src/Document.h
    ../src/Document.cpp
    ../src/TextCodec.h
    ../src/TextCodec.cpp
    ${editor_sources}
)
target_include_directories(tst_document PRIVATE ../src ../src/editor)
target_link_libraries(tst_document PRIVATE Qt6::Quick Qt6::Test KF6::SyntaxHighlighting "-framework CoreFoundation")
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
qt_add_resources(tst_syntaxhighlighter syntax_themes
    PREFIX "/kode/syntax"
    BASE ../assets
    FILES
        ../assets/themes/kode-dark.theme
        ../assets/themes/kode-light.theme
)
target_link_libraries(tst_syntaxhighlighter PRIVATE Qt6::Gui Qt6::Test KF6::SyntaxHighlighting)
add_test(NAME tst_syntaxhighlighter COMMAND tst_syntaxhighlighter)
set_tests_properties(tst_syntaxhighlighter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_matching
    tst_matching.h
    tst_matching.cpp
    ../src/editor/Matching.h
    ../src/editor/Matching.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
)
target_include_directories(tst_matching PRIVATE ../src/editor)
target_link_libraries(tst_matching PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_matching COMMAND tst_matching)

qt_add_executable(tst_textsearch
    tst_textsearch.h
    tst_textsearch.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
    ../src/editor/TextSearch.h
    ../src/editor/TextSearch.cpp
)
target_include_directories(tst_textsearch PRIVATE ../src/editor)
target_link_libraries(tst_textsearch PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_textsearch COMMAND tst_textsearch)

qt_add_executable(tst_indentation
    tst_indentation.h
    tst_indentation.cpp
    ../src/editor/Indentation.h
    ../src/editor/Indentation.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
)
target_include_directories(tst_indentation PRIVATE ../src/editor)
target_link_libraries(tst_indentation PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_indentation COMMAND tst_indentation)

qt_add_executable(tst_wrapindex
    tst_wrapindex.h
    tst_wrapindex.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
    ../src/editor/WrapIndex.h
    ../src/editor/WrapIndex.cpp
)
target_include_directories(tst_wrapindex PRIVATE ../src/editor)
target_link_libraries(tst_wrapindex PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_wrapindex COMMAND tst_wrapindex)

qt_add_executable(tst_preferences
    tst_preferences.h
    tst_preferences.cpp
    ../src/Preferences.h
    ../src/Preferences.cpp
)
target_include_directories(tst_preferences PRIVATE ../src)
target_link_libraries(tst_preferences PRIVATE Qt6::Gui Qt6::Qml Qt6::Test)
add_test(NAME tst_preferences COMMAND tst_preferences)
set_tests_properties(tst_preferences PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_splits
    tst_splits.h
    tst_splits.cpp
    ../src/Splits.h
    ../src/Splits.cpp
    ../src/OpenDocuments.h
    ../src/OpenDocuments.cpp
    ../src/FileTypes.h
    ../src/FileTypes.mm
    ../src/DocumentHistory.h
    ../src/DocumentHistory.cpp
    ../src/Document.h
    ../src/Document.cpp
    ../src/TextCodec.h
    ../src/TextCodec.cpp
    ${editor_sources}
    ${lsp_sources}
)
target_include_directories(tst_splits PRIVATE ../src ../src/editor ../src/lsp)
target_link_libraries(tst_splits PRIVATE Qt6::Quick Qt6::Test KF6::SyntaxHighlighting "-framework CoreFoundation" "-framework Foundation" "-framework UniformTypeIdentifiers")
set_source_files_properties(../src/FileTypes.mm TARGET_DIRECTORY tst_splits PROPERTIES COMPILE_OPTIONS -fobjc-arc)
add_test(NAME tst_splits COMMAND tst_splits)
set_tests_properties(tst_splits PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_folding
    tst_folding.h
    tst_folding.cpp
    ../src/editor/Folding.h
    ../src/editor/Folding.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
)
target_include_directories(tst_folding PRIVATE ../src/editor)
target_link_libraries(tst_folding PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_folding COMMAND tst_folding)

qt_add_executable(tst_filetree
    tst_filetree.h
    tst_filetree.cpp
    ../src/FileTree.h
    ../src/FileTree.cpp
    ../src/FileSearch.h
    ../src/FileSearch.cpp
    ../src/FileOperations.h
    ../src/FileOperations.cpp
    ../src/FileKinds.h
    ../src/FileKinds.cpp
)
target_include_directories(tst_filetree PRIVATE ../src)
target_link_libraries(tst_filetree PRIVATE Qt6::Gui Qt6::Qml Qt6::Test)
add_test(NAME tst_filetree COMMAND tst_filetree)
set_tests_properties(tst_filetree PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_commenting
    tst_commenting.h
    tst_commenting.cpp
    ../src/editor/Commenting.h
    ../src/editor/Commenting.cpp
    ../src/editor/Indentation.h
    ../src/editor/Indentation.cpp
    ../src/editor/TextBuffer.h
    ../src/editor/TextBuffer.cpp
)
target_include_directories(tst_commenting PRIVATE ../src/editor)
target_link_libraries(tst_commenting PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_commenting COMMAND tst_commenting)

qt_add_executable(tst_lsp
    tst_lsp.h
    tst_lsp.cpp
    ../src/Document.h
    ../src/Document.cpp
    ../src/TextCodec.h
    ../src/TextCodec.cpp
    ${editor_sources}
    ${lsp_sources}
)
target_include_directories(tst_lsp PRIVATE ../src ../src/lsp ../src/editor)
target_link_libraries(tst_lsp PRIVATE Qt6::Quick Qt6::Test KF6::SyntaxHighlighting "-framework CoreFoundation")
add_test(NAME tst_lsp COMMAND tst_lsp)
set_tests_properties(tst_lsp PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
