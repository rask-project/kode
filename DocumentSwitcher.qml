pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Rask.Platform

import Tekst

// The Ctrl+Tab switcher, as in Qt Creator: a split's documents by recent
// use. Ctrl+Tab (Ctrl+Shift+Tab) opens it on the next (last) one and moves
// on while Control stays down; releasing Control shows the one selected,
// Escape closes it without a change.
Popup {
    id: switcher

    // Most recently shown first (DocumentHistory.recent).
    property list<Document> documents: []
    property int current: 0

    signal chosen(Document document)

    // Opens it, or moves the selection by `step` while open.
    function step(delta: int) {
        const count = switcher.documents.length;
        if (count < 2)
            return;
        if (!switcher.opened) {
            switcher.current = delta > 0 ? 1 : count - 1;
            switcher.open();
            return;
        }
        switcher.current = (switcher.current + delta + count) % count;
    }

    function choose() {
        const document = switcher.documents[switcher.current] ?? null;
        switcher.close();
        if (document)
            switcher.chosen(document);
    }

    width: 360
    height: Math.min(list.contentHeight, 12 * 28) + topPadding + bottomPadding
    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnPressOutside

    background: Rectangle {
        radius: 10
        color: Appearance.dark ? "#2b2b2b" : "#f6f6f6"
        border.width: 1
        border.color: Qt.alpha(SystemColors.label, 0.15)
    }

    contentItem: ListView {
        id: list

        clip: true
        focus: true
        model: switcher.documents
        currentIndex: switcher.current
        highlightMoveDuration: 0
        boundsBehavior: Flickable.StopAtBounds

        highlight: Rectangle {
            radius: 6
            color: SystemColors.controlAccent
        }

        delegate: Item {
            id: row

            required property Document modelData
            required property int index

            width: ListView.view.width
            height: 28

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.modified ? row.modelData.fileName + " •" : row.modelData.fileName
                color: row.index === switcher.current ? "white" : SystemColors.label
                elide: Text.ElideMiddle
            }

            TapHandler {
                onTapped: {
                    switcher.current = row.index;
                    switcher.choose();
                }
            }
        }

        // On macOS Control is Qt's Meta.
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Meta || !(event.modifiers & Qt.MetaModifier))
                switcher.choose();
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Tab) {
                switcher.step(1);
            } else if (event.key === Qt.Key_Backtab) {
                switcher.step(-1);
            } else if (event.key === Qt.Key_Escape) {
                switcher.close();
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                switcher.choose();
            } else {
                return;
            }
            event.accepted = true;
        }
    }
}
