pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Effects

import Rask.Platform

import Kode

// ⌘K, as Qt Creator's Locate: a field and the folder's files whose name
// matches what is typed, best first, each with its path. ↑ ↓ choose,
// Return opens, Escape closes. In the middle of the window, over a blur of
// what is behind it (`behind`).
Popup {
    id: finder

    required property url folder
    // What it blurs: the window's content.
    property Item behind: null

    signal chosen(url file)

    function choose(row: int) {
        const file = search.fileAt(row);
        finder.close();
        if (file.toString() !== "")
            finder.chosen(file);
    }

    readonly property int rowHeight: 26
    readonly property int shownRows: Math.min(list.count, 14)

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.15)
    width: Math.min(820, parent.width - 80)
    padding: 12
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // The last search, selected to type over.
    onOpened: {
        field.forceActiveFocus();
        field.selectAll();
    }

    FileSearch {
        id: search

        folder: finder.folder
        showHidden: Preferences.showHiddenFiles
        active: finder.visible
        query: field.text
        markColor: Qt.alpha(SystemColors.findHighlight, 0.55)
        onModelReset: list.currentIndex = 0
    }

    background: Item {
        ShaderEffectSource {
            id: captured

            anchors.fill: parent
            // Only while shown: a window destroyed with its content still captured crashes in Qt.
            sourceItem: finder.visible ? finder.behind : null
            sourceRect: Qt.rect(finder.x, finder.y, finder.width, finder.height)
            visible: false
        }

        Rectangle {
            id: corners

            anchors.fill: parent
            radius: 12
            visible: false
            layer.enabled: true
        }

        // Opaque under the blur: the window's content shows only blurred, not
        // through it (the editor's own background may be transparent).
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: Appearance.dark ? "#1e1e1e" : "#ececec"
        }

        MultiEffect {
            anchors.fill: parent
            source: captured
            blurEnabled: true
            blur: 1.0
            blurMax: 64
            maskEnabled: true
            maskSource: corners
        }

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: Qt.alpha(Appearance.dark ? "#1f1f1f" : "#f5f5f5", 0.55)
            border.width: 1
            border.color: Qt.alpha(SystemColors.label, 0.15)
        }
    }

    contentItem: Column {
        spacing: 12

        // As Spotlight's: the magnifying glass, then a field without a frame.
        Item {
            width: parent.width
            height: 36

            IconImage {
                id: magnifier

                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                name: "magnifyingglass"
                sourceSize.width: 20
                sourceSize.height: 20
                color: SystemColors.secondaryLabel
            }

            TextField {
                id: field

                anchors.left: magnifier.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                background: null
                leftPadding: 0
                rightPadding: 0
                color: SystemColors.label
                placeholderTextColor: SystemColors.tertiaryLabel
                placeholderText: qsTr("Find a file in %1").arg(decodeURIComponent(finder.folder.toString().replace(/\/$/, "").split("/").pop()))
                font.pixelSize: 22
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down)
                        list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1);
                    else if (event.key === Qt.Key_Up)
                        list.currentIndex = Math.max(0, list.currentIndex - 1);
                    else if (event.key === Qt.Key_PageDown)
                        list.currentIndex = Math.min(list.count - 1, list.currentIndex + finder.shownRows);
                    else if (event.key === Qt.Key_PageUp)
                        list.currentIndex = Math.max(0, list.currentIndex - finder.shownRows);
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        finder.choose(list.currentIndex);
                    else if (event.key === Qt.Key_Escape)
                        finder.close();
                    else
                        return;
                    event.accepted = true;
                }
            }
        }

        // A hairline between the field and the list.
        Rectangle {
            width: parent.width
            height: 1
            visible: list.count > 0 || field.text.trim() !== ""
            color: Qt.alpha(SystemColors.label, 0.12)
        }

        // While typing: the folder still being listed, or nothing found.
        Label {
            width: parent.width
            visible: field.text.trim() !== "" && list.count === 0
            height: visible ? 30 : 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: search.listing ? qsTr("Listing files…") : qsTr("No files found")
            color: SystemColors.secondaryLabel
        }

        ListView {
            id: list

            width: parent.width
            height: finder.shownRows * finder.rowHeight
            clip: true
            model: search
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds

            highlight: Rectangle {
                radius: 6
                color: SystemColors.controlAccent
            }

            delegate: Item {
                id: row

                required property int index
                required property string markedName
                required property string path
                required property string iconSource

                readonly property bool current: ListView.isCurrentItem

                width: ListView.view.width
                height: finder.rowHeight

                Image {
                    id: icon

                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    sourceSize.width: 32
                    sourceSize.height: 32
                    asynchronous: !row.iconSource.startsWith("image://fileicon")
                    source: row.iconSource
                }

                Text {
                    id: name

                    anchors.left: icon.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width * 0.4)
                    elide: Text.ElideRight
                    textFormat: Text.RichText
                    text: row.markedName
                    color: row.current ? "white" : SystemColors.label
                    font.pixelSize: 13
                }

                Text {
                    anchors.left: name.right
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    // The end of a long path is what tells it apart.
                    elide: Text.ElideLeft
                    text: row.path
                    color: row.current ? Qt.alpha("white", 0.8) : SystemColors.secondaryLabel
                    font.pixelSize: 12
                }

                TapHandler {
                    onTapped: finder.choose(row.index)
                }
            }
        }
    }
}
