pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Shapes

import Rask.Controls
import Rask.Platform

import Tekst

// One split of a window, as in Qt Creator: a bar with the open documents
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

    Component.onCompleted: {
        if (!pane.document && pane.initialFile.toString() !== "")
            pane.document = OpenDocuments.open(pane.initialFile);
        if (!pane.document)
            pane.document = OpenDocuments.other(null) ?? OpenDocuments.create();
    }

    // Flat, a light fill under the pointer (the macOS style draws a bezel).
    component BarButton: T.ToolButton {
        id: barButton

        implicitWidth: 26
        implicitHeight: 22
        padding: 4
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        background: Rectangle {
            radius: 5
            color: barButton.hovered ? Qt.alpha(SystemColors.label, barButton.down ? 0.16 : 0.08) : "transparent"
        }
    }

    // The bar: documents, close, splits.
    Rectangle {
        id: bar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 28
        color: Qt.alpha(SystemColors.label, pane.active ? 0.06 : 0.03)

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.alpha(SystemColors.label, 0.1)
        }

        // Which document the split shows, from every open one.
        BarButton {
            id: documentButton

            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: documentRow.implicitWidth + 16
            Accessible.name: qsTr("Open documents")
            onClicked: documentsMenu.popup(documentButton, 0, documentButton.height)

            contentItem: Item {
                Row {
                    id: documentRow

                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        // Room left by the split buttons.
                        width: Math.min(implicitWidth, Math.max(40, splitButtons.x - documentButton.x - closeButton.width - 40))
                        text: pane.modified ? pane.title + " •" : pane.title
                        color: pane.active ? SystemColors.label : SystemColors.secondaryLabel
                        elide: Text.ElideMiddle
                    }

                    // The pop-up arrows: a chevron up over a chevron down.
                    Shape {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 7
                        height: 10
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            strokeColor: SystemColors.secondaryLabel
                            strokeWidth: 1.3
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin

                            PathMove {
                                x: 0.5
                                y: 3.5
                            }
                            PathLine {
                                x: 3.5
                                y: 0.5
                            }
                            PathLine {
                                x: 6.5
                                y: 3.5
                            }
                            PathMove {
                                x: 0.5
                                y: 6.5
                            }
                            PathLine {
                                x: 3.5
                                y: 9.5
                            }
                            PathLine {
                                x: 6.5
                                y: 6.5
                            }
                        }
                    }
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
            Accessible.name: qsTr("Close document")
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Close Document")
            onClicked: pane.closeDocumentRequested()

            contentItem: Glyph {
                kind: "close"
            }
        }

        Row {
            id: splitButtons

            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            BarButton {
                Accessible.name: qsTr("Split side by side")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Split Side by Side")
                onClicked: pane.splitRequested(Qt.Horizontal)

                contentItem: Glyph {
                    kind: "sideBySide"
                }
            }
            BarButton {
                Accessible.name: qsTr("Split top and bottom")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Split Top and Bottom")
                onClicked: pane.splitRequested(Qt.Vertical)

                contentItem: Glyph {
                    kind: "topAndBottom"
                }
            }
            BarButton {
                visible: pane.removable
                Accessible.name: qsTr("Remove split")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Remove Split")
                onClicked: pane.removeRequested()

                contentItem: Glyph {
                    kind: "removeSplit"
                }
            }
        }

        TapHandler {
            onTapped: {
                pane.activated();
                pane.focusEditor();
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

    // The bar's glyphs, drawn: × and the split icons.
    component Glyph: Item {
        id: glyph

        required property string kind

        implicitWidth: 14
        implicitHeight: 14

        // ×: in "close" and over the box in "removeSplit".
        Repeater {
            model: glyph.kind === "close" ? [45, -45] : []

            Rectangle {
                required property int modelData

                anchors.centerIn: parent
                width: 10
                height: 1.5
                radius: 0.75
                rotation: modelData
                color: SystemColors.secondaryLabel
            }
        }

        // A window with its halves.
        Rectangle {
            anchors.centerIn: parent
            width: 13
            height: 11
            visible: glyph.kind !== "close"
            radius: 2
            color: "transparent"
            border.width: 1.2
            border.color: SystemColors.secondaryLabel

            Rectangle {
                anchors.centerIn: parent
                width: glyph.kind === "sideBySide" ? 1.2 : parent.width
                height: glyph.kind === "topAndBottom" ? 1.2 : parent.height
                visible: glyph.kind === "sideBySide" || glyph.kind === "topAndBottom"
                color: SystemColors.secondaryLabel
            }

            Repeater {
                model: glyph.kind === "removeSplit" ? [45, -45] : []

                Rectangle {
                    required property int modelData

                    anchors.centerIn: parent
                    width: 7
                    height: 1.2
                    rotation: modelData
                    color: SystemColors.secondaryLabel
                }
            }
        }
    }
}
