# The Stage 3 benchmark: TextEdit vs. the EditorView prototype (see run.sh).
qt_add_executable(tekst_editor_bench
    main.cpp
    ../../src/editor/TextBuffer.h
    ../../src/editor/TextBuffer.cpp
)

qt_add_qml_module(tekst_editor_bench
    URI TekstEditorBench
    QML_FILES
        TextEditCase.qml
        EditorViewCase.qml
    SOURCES
        ../../src/editor/EditorAccessible.h
        ../../src/editor/EditorAccessible.cpp
        ../../src/editor/EditorView.h
        ../../src/editor/EditorView.cpp
        ../../src/editor/Indentation.h
        ../../src/editor/Indentation.cpp
        ../../src/editor/Matching.h
        ../../src/editor/Matching.cpp
        ../../src/editor/WrapIndex.h
        ../../src/editor/WrapIndex.cpp
        ../../src/editor/TextSearch.h
        ../../src/editor/TextSearch.cpp
        ../../src/editor/SyntaxHighlighter.h
        ../../src/editor/SyntaxHighlighter.cpp
)

set_target_properties(tekst_editor_bench PROPERTIES OUTPUT_NAME tekst-editor-bench)
target_include_directories(tekst_editor_bench PRIVATE ../../src/editor)
target_link_libraries(tekst_editor_bench PRIVATE Qt6::Quick KF6::SyntaxHighlighting)
