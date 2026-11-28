pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

import Rask.Controls
import Rask.Platform

import Kode

// File > Connect to Server…, as the Finder's and an open panel: a host
// from ~/.ssh/config, or typed as ssh takes it, reached over SSH (Hosts);
// where it stands, and once online its files (RemoteFolder) under a path
// bar: a double click (or Return) enters a folder or opens a file in
// `window`; Open Folder makes the folder in view the window's.
Popup {
    id: root

    property EditorWindow window: null
    property Host host: null
    readonly property bool online: root.host !== null && root.host.state === Host.Online
    readonly property bool busy: root.host !== null && (root.host.state === Host.Connecting || root.host.state === Host.Installing)
    readonly property bool connected: root.host !== null && root.host.destination === hostField.editText.trim() && root.host.state !== Host.Disconnected && root.host.state !== Host.Failed
    // The folder in view by its parts, the host's root first: each {name, path}.
    readonly property var crumbs: {
        const parts = folderModel.folder.split("/").filter(part => part !== "");
        return [
            {
                name: root.host?.destination ?? "",
                path: "/"
            }
        ].concat(parts.map((part, index) => ({
                    name: part,
                    path: "/" + parts.slice(0, index + 1).join("/")
                })));
    }

    function connectHost() {
        // Listed in Settings > Remote from now on.
        Preferences.addRemoteHost(hostField.editText);
        root.host = Hosts.host(hostField.editText);
        root.host.connectToHost();
    }

    function openEntry(isFolder: bool, path: string, file: url) {
        if (isFolder) {
            folderModel.folder = path;
            entries.currentIndex = -1;
            return;
        }
        root.window.app.openFiles([file], root.window);
        root.close();
    }

    function openCurrent() {
        const item = entries.currentItem as EntryRow;
        if (item)
            root.openEntry(item.isFolder, item.path, item.fileUrl);
    }

    component EntryRow: Item {
        required property int index
        required property string name
        required property bool isFolder
        required property string path
        required property url fileUrl
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.12)
    width: 560
    padding: 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        hostField.model = Hosts.configuredHosts();
        hostField.forceActiveFocus();
    }

    // As a sheet: the window's color, rounded, a hairline around.
    background: Rectangle {
        radius: 12
        color: Appearance.dark ? "#2b2b2b" : "#ececec"
        border.width: 1
        border.color: Qt.alpha(SystemColors.label, 0.14)
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
            color: SystemColors.label
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                id: hostField

                Layout.fillWidth: true
                editable: true
                Accessible.name: qsTr("Server")
                onAccepted: root.connectHost()
            }
            Button {
                enabled: hostField.editText.trim() !== ""
                text: root.connected ? qsTr("Disconnect") : qsTr("Connect")
                highlighted: !root.connected
                onClicked: root.connected ? root.host.disconnectFromHost() : root.connectHost()
            }
        }

        // Where it stands: a dot (green online, gray otherwise), the status.
        RowLayout {
            Layout.fillWidth: true
            visible: root.host !== null
            spacing: 6

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: root.online ? (Appearance.dark ? "#30d158" : "#28cd41") : root.host?.state === Host.Failed ? (Appearance.dark ? "#ff453a" : "#ff3b30") : SystemColors.tertiaryLabel
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: root.host?.status ?? ""
                font.pointSize: 11
                color: SystemColors.secondaryLabel
            }
            BusyIndicator {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                visible: running
                running: root.busy || folderModel.loading
            }
        }

        // The path bar: the enclosing folder, then each part of the path.
        RowLayout {
            Layout.fillWidth: true
            visible: root.online
            spacing: 4

            ToolButton {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 22
                flat: true
                focusPolicy: Qt.NoFocus
                icon.name: "chevron.left"
                icon.width: 12
                icon.height: 12
                enabled: folderModel.parentFolder !== ""
                Accessible.name: qsTr("Enclosing Folder")
                onClicked: {
                    folderModel.folder = folderModel.parentFolder;
                    entries.currentIndex = -1;
                }
            }

            Flickable {
                id: pathBar

                Layout.fillWidth: true
                Layout.preferredHeight: 22
                clip: true
                contentWidth: crumbRow.width
                interactive: contentWidth > width
                boundsBehavior: Flickable.StopAtBounds
                // The deepest part in view.
                onContentWidthChanged: contentX = Math.max(0, contentWidth - width)

                Row {
                    id: crumbRow

                    height: parent.height
                    spacing: 2

                    Repeater {
                        model: root.crumbs

                        delegate: Row {
                            id: crumb

                            required property var modelData
                            required property int index
                            readonly property bool last: index === root.crumbs.length - 1

                            height: crumbRow.height
                            spacing: 2

                            IconImage {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: crumb.index > 0
                                name: "chevron.right"
                                sourceSize.width: 8
                                sourceSize.height: 8
                                color: SystemColors.tertiaryLabel
                            }
                            IconImage {
                                anchors.verticalCenter: parent.verticalCenter
                                name: crumb.index === 0 ? "server.rack" : "folder"
                                sourceSize.width: 13
                                sourceSize.height: 13
                                color: crumb.index === 0 ? SystemColors.secondaryLabel : SystemColors.controlAccent
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: crumb.modelData.name
                                font.pointSize: 11
                                color: crumb.last ? SystemColors.label : (crumbHover.hovered ? SystemColors.label : SystemColors.secondaryLabel)

                                HoverHandler {
                                    id: crumbHover

                                    enabled: !crumb.last
                                }
                                TapHandler {
                                    enabled: !crumb.last
                                    onTapped: {
                                        folderModel.folder = crumb.modelData.path;
                                        entries.currentIndex = -1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // The folder's entries, framed as an open panel's list.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            visible: root.online
            radius: 6
            color: Appearance.dark ? "#1e1e1e" : "#ffffff"
            border.width: 1
            border.color: Qt.alpha(SystemColors.label, 0.12)

            ListView {
                id: entries

                anchors.fill: parent
                anchors.margins: 5
                clip: true
                focus: true
                model: folderModel
                currentIndex: -1
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                ScrollBar.vertical: OverlayScrollBar {}
                Keys.onReturnPressed: root.openCurrent()
                Keys.onEnterPressed: root.openCurrent()

                delegate: EntryRow {
                    id: entry

                    readonly property bool selected: ListView.isCurrentItem

                    width: ListView.view.width
                    height: 24

                    // As the system's lists: rounded, the accent while the list has the focus.
                    Rectangle {
                        anchors.fill: parent
                        radius: 5
                        visible: entry.selected
                        color: entries.activeFocus ? SystemColors.controlAccent : Qt.alpha(SystemColors.label, 0.12)
                    }

                    Image {
                        id: icon

                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16
                        height: 16
                        sourceSize.width: 32
                        sourceSize.height: 32
                        source: entry.isFolder ? "image://fileicon/:folder" : "image://fileicon/:file"
                    }
                    Label {
                        anchors.left: icon.right
                        anchors.leftMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideMiddle
                        text: entry.name
                        color: entry.selected && entries.activeFocus ? "white" : SystemColors.label
                    }

                    TapHandler {
                        onTapped: {
                            entries.currentIndex = entry.index;
                            entries.forceActiveFocus();
                        }
                        onDoubleTapped: root.openEntry(entry.isFolder, entry.path, entry.fileUrl)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: folderModel.errorString !== "" || (!folderModel.loading && entries.count === 0)
                text: folderModel.errorString !== "" ? folderModel.errorString : qsTr("Empty folder")
                color: SystemColors.secondaryLabel
            }
        }

        // As macOS orders them: Cancel, then the default button last.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            Item {
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            Button {
                visible: root.online
                text: qsTr("Open Folder")
                onClicked: {
                    root.window.app.openFolder(root.host.fileUrl(folderModel.folder), root.window);
                    root.close();
                }
            }
            Button {
                visible: root.online
                enabled: entries.currentIndex >= 0
                highlighted: true
                text: qsTr("Open")
                onClicked: root.openCurrent()
            }
        }
    }
}
