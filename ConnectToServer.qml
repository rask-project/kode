pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Rask.Platform

import Kode

// File > Connect to Server…: a host from ~/.ssh/config, or typed as ssh
// takes it, reached over SSH (Hosts); where it stands below, and once
// online its files (RemoteFolder): a double click enters a folder or opens
// a file in `window`.
Popup {
    id: root

    property EditorWindow window: null
    property Host host: null
    readonly property bool online: root.host !== null && root.host.state === Host.Online

    function connectHost() {
        root.host = Hosts.host(hostField.editText);
        root.host.connectToHost();
    }

    function openEntry(isFolder: bool, path: string, file: url) {
        if (isFolder) {
            folderModel.folder = path;
            return;
        }
        root.window.app.openFiles([file], root.window);
        root.close();
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.15)
    width: 520
    padding: 16
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        hostField.model = Hosts.configuredHosts();
        hostField.forceActiveFocus();
    }

    background: Rectangle {
        radius: 10
        color: root.palette.window
        border.color: Qt.alpha(SystemColors.label, 0.15)
    }

    RemoteFolder {
        id: folderModel

        host: root.host
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: qsTr("Connect to Server")
            font.bold: true
        }
        ComboBox {
            id: hostField

            Layout.fillWidth: true
            editable: true
            Accessible.name: qsTr("Host")
            onAccepted: root.connectHost()
        }
        Label {
            Layout.fillWidth: true
            visible: root.host !== null
            wrapMode: Text.Wrap
            text: root.host?.status ?? ""
            color: root.palette.placeholderText
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.online

            Button {
                icon.name: "chevron.up"
                enabled: folderModel.parentFolder !== ""
                Accessible.name: qsTr("Enclosing Folder")
                onClicked: folderModel.folder = folderModel.parentFolder
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: folderModel.folder
            }
            BusyIndicator {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                running: folderModel.loading
            }
        }
        ListView {
            id: entries

            Layout.fillWidth: true
            Layout.preferredHeight: 300
            visible: root.online
            clip: true
            model: folderModel
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: entry

                required property int index
                required property string name
                required property bool isFolder
                required property string path
                required property url fileUrl

                width: ListView.view.width
                text: entry.name
                icon.name: entry.isFolder ? "folder" : "doc"
                highlighted: ListView.isCurrentItem
                onClicked: entries.currentIndex = entry.index
                onDoubleClicked: root.openEntry(entry.isFolder, entry.path, entry.fileUrl)
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.online && folderModel.errorString !== ""
            wrapMode: Text.Wrap
            text: folderModel.errorString
        }
        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Close")
                onClicked: root.close()
            }
            Button {
                readonly property bool connected: root.host !== null && root.host.destination === hostField.editText.trim() && root.host.state !== Host.Disconnected && root.host.state !== Host.Failed

                enabled: hostField.editText.trim() !== ""
                text: connected ? qsTr("Disconnect") : qsTr("Connect")
                highlighted: !root.online
                onClicked: connected ? root.host.disconnectFromHost() : root.connectHost()
            }
            Button {
                visible: root.online
                enabled: entries.currentIndex >= 0
                highlighted: true
                text: qsTr("Open")
                onClicked: {
                    const item = entries.currentItem as ItemDelegate;
                    if (item)
                        item.doubleClicked();
                }
            }
        }
    }
}
