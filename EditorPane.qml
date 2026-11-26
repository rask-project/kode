pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Controls.impl

import Rask.Controls
import Rask.Platform

import Tekst

// One split of a window: a jump bar as in Xcode with the open documents
// (pick the one shown), close document and the split buttons, then the find
// bar, the editor and the status bar. It shows one of OpenDocuments; other
// splits may show the same one, each with its own caret and scroll.
Item {
    id: pane

    // A restored split: the document of this file.
    property url initialFile
    property Document document: null
    property bool active: false
    // The window has more than one split.
    property bool removable: false

    readonly property EditorView editor: textEditor.editor
    // The documents this split showed, for Back and Forward.
    readonly property DocumentHistory history: documentHistory
    readonly property url fileUrl: document ? document.fileUrl : ""
    readonly property string title: document ? document.fileName : ""
    readonly property bool modified: document ? document.modified : false
    // A new document nobody typed in: opening a file replaces it.
    readonly property bool untouched: document !== null && document.fileUrl.toString() === "" && !document.modified && document.textDocument.length === 0

    // The split wants focus (a click, the editor focused).
    signal activated
    signal documentChosen(Document document)
    // Run `action` once this document's unsaved changes are saved or discarded.
    signal reviewRequested(var action)
    signal closeDocumentRequested
    signal splitRequested(int orientation)
    signal removeRequested

    function focusEditor() {
        textEditor.editor.forceActiveFocus();
    }

    // Shows the find bar (with the replace row when `replacing`), focused.
    function showSearch(replacing: bool) {
        searchBar.replacing = replacing;
        searchBar.visible = true;
        searchBar.focusFind();
    }

    function useSelectionForFind() {
        searchBar.text = textEditor.editor.selectedText();
    }

    function showGoToLine() {
        goToLine.open();
    }

    function goBack() {
        const document = documentHistory.back();
        if (document)
            pane.documentChosen(document);
    }

    function goForward() {
        const document = documentHistory.forward();
        if (document)
            pane.documentChosen(document);
    }

    onDocumentChanged: documentHistory.visit(pane.document)

    DocumentHistory {
        id: documentHistory
    }

    Connections {
        target: OpenDocuments

        // Showing it, the split goes back to the one before (none: another one).
        function onDocumentClosing(document: Document) {
            documentHistory.forget(document);
            if (pane.document === document)
                pane.document = documentHistory.current() ?? OpenDocuments.other(document);
        }
    }

    Component.onCompleted: {
        if (!pane.document && pane.initialFile.toString() !== "")
            pane.document = OpenDocuments.open(pane.initialFile);
        if (!pane.document)
            pane.document = OpenDocuments.other(null) ?? OpenDocuments.create();
    }

    // Flat, an SF Symbol and a light fill under the pointer (the macOS style draws a bezel).
    component BarButton: T.ToolButton {
        id: barButton

        implicitWidth: 28
        implicitHeight: 26
        padding: 0
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        icon.width: 15
        icon.height: 15
        ToolTip.visible: hovered && ToolTip.text !== ""
        ToolTip.delay: 600

        background: Rectangle {
            radius: 6
            color: barButton.hovered ? Qt.alpha(SystemColors.label, barButton.down ? 0.16 : 0.08) : "transparent"
        }

        contentItem: IconImage {
            name: barButton.icon.name
            sourceSize.width: barButton.icon.width
            sourceSize.height: barButton.icon.height
            fillMode: Image.Pad
            color: barButton.hovered ? SystemColors.label : SystemColors.secondaryLabel
        }
    }

    // The jump bar, as in Xcode: which document (from every open one), close
    // it, and the split buttons.
    Item {
        id: bar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 34

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.alpha(SystemColors.label, 0.1)
        }

        TapHandler {
            onTapped: {
                pane.activated();
                pane.focusEditor();
            }
        }

        // A pop-up button: the document's icon and name, the menu's arrows at its end.
        BarButton {
            id: documentButton

            // Room left by the other buttons.
            readonly property real room: splitButtons.x - x - closeButton.width - 16

            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            // The title's own width plus the icon, the arrows, spacing and padding.
            width: Math.max(80, Math.min(Math.max(240, titleLabel.implicitWidth + 54), room))
            leftPadding: 8
            rightPadding: 8
            Accessible.name: qsTr("Open documents")
            // The file's path.
            ToolTip.text: pane.fileUrl.toString().startsWith("file://") ? decodeURIComponent(pane.fileUrl.toString().slice(7)) : ""
            onClicked: documentsMenu.popup(documentButton, 0, documentButton.height)

            contentItem: Item {
                Row {
                    id: documentRow

                    anchors.left: parent.left
                    anchors.right: arrows.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    IconImage {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "doc.text"
                        sourceSize.width: 15
                        sourceSize.height: 15
                        fillMode: Image.Pad
                        color: SystemColors.secondaryLabel
                    }

                    Label {
                        id: titleLabel

                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, documentRow.width - 21)
                        text: pane.modified ? pane.title + " •" : pane.title
                        color: pane.active ? SystemColors.label : SystemColors.secondaryLabel
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                    }
                }

                IconImage {
                    id: arrows

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron.up.chevron.down"
                    sourceSize.width: 11
                    sourceSize.height: 11
                    fillMode: Image.Pad
                    color: SystemColors.secondaryLabel
                }
            }
        }

        Menu {
            id: documentsMenu

            popupType: Popup.Native

            Instantiator {
                model: OpenDocuments.sortedDocuments

                delegate: MenuItem {
                    required property Document modelData

                    text: modelData.modified ? modelData.fileName + " •" : modelData.fileName
                    checkable: true
                    checked: modelData === pane.document
                    onTriggered: pane.documentChosen(modelData)
                }

                onObjectAdded: (index, object) => documentsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => documentsMenu.removeItem(object)
            }
        }

        BarButton {
            id: closeButton

            anchors.left: documentButton.right
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            icon.name: "xmark"
            icon.width: 12
            icon.height: 12
            Accessible.name: qsTr("Close document")
            ToolTip.text: qsTr("Close Document")
            onClicked: pane.closeDocumentRequested()
        }

        Row {
            id: splitButtons

            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            BarButton {
                icon.name: "square.split.2x1"
                Accessible.name: qsTr("Split side by side")
                ToolTip.text: qsTr("Split Side by Side")
                onClicked: pane.splitRequested(Qt.Horizontal)
            }
            BarButton {
                icon.name: "square.split.1x2"
                Accessible.name: qsTr("Split top and bottom")
                ToolTip.text: qsTr("Split Top and Bottom")
                onClicked: pane.splitRequested(Qt.Vertical)
            }
            BarButton {
                visible: pane.removable
                icon.name: "xmark.rectangle"
                Accessible.name: qsTr("Remove split")
                ToolTip.text: qsTr("Remove Split")
                onClicked: pane.removeRequested()
            }
        }
    }

    SearchBar {
        id: searchBar

        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? implicitHeight : 0
        visible: false
        matchCount: textEditor.editor.searchMatchCount
        currentMatch: textEditor.editor.searchCurrent
        complete: textEditor.editor.searchComplete
        error: textEditor.editor.searchError
        onNext: textEditor.editor.findNext()
        onPrevious: textEditor.editor.findPrevious()
        onReplace: textEditor.editor.replaceCurrent(replacementText)
        onReplaceAll: textEditor.editor.replaceAll(replacementText)
        onDone: {
            visible = false;
            pane.focusEditor();
        }
    }

    GoToLine {
        id: goToLine

        x: (pane.width - width) / 2
        y: searchBar.y + searchBar.height + 8
        onAccepted: target => {
            if (textEditor.editor.goToLine(target)) {
                goToLine.close();
                textEditor.center(textEditor.editor.cursorRectangle);
            }
        }
        onClosed: pane.focusEditor()
    }

    // The editor searches for what the bar holds while it shows.
    Binding {
        target: textEditor.editor
        property: "searchPattern"
        value: searchBar.visible ? searchBar.text : ""
    }
    Binding {
        target: textEditor.editor
        property: "searchCaseSensitive"
        value: searchBar.caseSensitive
    }
    Binding {
        target: textEditor.editor
        property: "searchRegularExpression"
        value: searchBar.regularExpression
    }

    TextEditor {
        id: textEditor

        anchors.top: searchBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        editor.document: pane.document ? pane.document.textDocument : null
        editor.syntaxTheme: Syntaxes.themeName(Preferences.theme, Appearance.dark)
        editor.wrap: Preferences.wrap
        editor.showInvisibles: Preferences.showInvisibles
        editor.font: Preferences.font
        editor.defaultIndentWithTabs: Preferences.indentWithTabs
        editor.defaultIndentWidth: Preferences.indentWidth
        editor.onActiveFocusChanged: {
            if (textEditor.editor.activeFocus)
                pane.activated();
        }
    }

    Loader {
        id: statusBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 26
        active: pane.document !== null

        sourceComponent: StatusBar {
            editor: textEditor.editor
            document: pane.document
            onReopenRequested: encoding => pane.reviewRequested(() => pane.document.reopen(encoding))
        }
    }
}
