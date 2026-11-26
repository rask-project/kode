pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Rask.Platform

// What a split without a document shows, as in Qt Creator: how to open one,
// with buttons for New, Open and Open Folder.
Item {
    id: page

    signal newRequested
    signal openRequested
    signal openFolderRequested

    Column {
        anchors.centerIn: parent
        width: Math.min(420, page.width - 48)
        spacing: 12

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Open a document")
            color: SystemColors.secondaryLabel
            font.pixelSize: 20
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8

            Button {
                text: qsTr("New")
                focusPolicy: Qt.TabFocus
                onClicked: page.newRequested()
            }
            Button {
                text: qsTr("Open…")
                focusPolicy: Qt.TabFocus
                onClicked: page.openRequested()
            }
            Button {
                text: qsTr("Open Folder…")
                focusPolicy: Qt.TabFocus
                onClicked: page.openFolderRequested()
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Qt.alpha(SystemColors.label, 0.25)
        }

        Repeater {
            model: [qsTr("Or from the File menu: ⌘N, ⌘O, ⇧⌘O"), qsTr("File > Open Recent"), qsTr("Switch between the open documents with ⌃Tab"), qsTr("Split with ⌘D (side by side) or ⇧⌘D (top and bottom)"), qsTr("Drag and drop files here")]

            Label {
                required property string modelData

                width: parent.width
                text: "•  " + modelData
                color: SystemColors.secondaryLabel
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
    }
}
