# The Stage 3 benchmark: TextEdit vs. the EditorView prototype (see run.sh).
qt_add_executable(kode_editor_bench
    main.cpp
    ../../src/editor/TextBuffer.h
    ../../src/editor/TextBuffer.cpp
)

qt_add_qml_module(kode_editor_bench
    URI KodeEditorBench
    QML_FILES
        TextEditCase.qml
        EditorViewCase.qml
    SOURCES
        ../../src/editor/EditorAccessible.h
        ../../src/editor/EditorAccessible.cpp
        ../../src/editor/EditorView.h
        ../../src/editor/EditorView.cpp
        ../../src/editor/Folding.h
        ../../src/editor/Folding.cpp
        ../../src/editor/Commenting.h
        ../../src/editor/Commenting.cpp
        ../../src/editor/Indentation.h
        ../../src/editor/Indentation.cpp
        ../../src/editor/Matching.h
        ../../src/editor/Matching.cpp
        ../../src/editor/Minimap.h
        ../../src/editor/Minimap.cpp
        ../../src/editor/WrapIndex.h
        ../../src/editor/WrapIndex.cpp
        ../../src/editor/TextSearch.h
        ../../src/editor/TextSearch.cpp
        ../../src/editor/SyntaxHighlighter.h
        ../../src/editor/SyntaxHighlighter.cpp
        ../../src/editor/TextDocument.h
        ../../src/editor/TextDocument.cpp
)

set_target_properties(kode_editor_bench PROPERTIES OUTPUT_NAME kode-editor-bench)
target_include_directories(kode_editor_bench PRIVATE ../../src/editor)
target_link_libraries(kode_editor_bench PRIVATE Qt6::Quick KF6::SyntaxHighlighting)

# Stage 10: the work that grows with the file, on the GUI thread (run-work.sh).
qt_add_executable(kode_work_bench
    work.cpp
    ../../src/Document.h
    ../../src/Document.cpp
    ../../src/TextCodec.h
    ../../src/TextCodec.cpp
    ../../src/editor/EditorAccessible.h
    ../../src/editor/EditorAccessible.cpp
    ../../src/editor/EditorView.h
    ../../src/editor/EditorView.cpp
    ../../src/editor/Folding.h
    ../../src/editor/Folding.cpp
    ../../src/editor/Commenting.h
    ../../src/editor/Commenting.cpp
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
    ../../src/editor/TextBuffer.h
    ../../src/editor/TextBuffer.cpp
    ../../src/editor/TextDocument.h
    ../../src/editor/TextDocument.cpp
    ../../remote/protocol/Frame.cpp
    ../../src/remote/Connection.cpp
    ../../src/remote/Files.cpp
    ../../src/remote/Host.cpp
    ../../src/remote/Hosts.cpp
    ../../src/remote/Ssh.cpp
)
set_target_properties(kode_work_bench PROPERTIES OUTPUT_NAME kode-work-bench)
target_include_directories(kode_work_bench PRIVATE ../../src ../../src/editor)
target_link_libraries(kode_work_bench PRIVATE Qt6::Network Qt6::Quick KF6::SyntaxHighlighting "-framework CoreFoundation")
target_compile_definitions(kode_work_bench PRIVATE KODE_VERSION="${KODE_DISPLAY_VERSION}")
