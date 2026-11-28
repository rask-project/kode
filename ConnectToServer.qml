pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Rask.Platform

import Kode

// File > Connect to Server…: a host from ~/.ssh/config, or typed as ssh
// takes it, reached over SSH (Hosts); where it stands below.
Popup {
    id: root

    property Host host: null

    function connectHost() {
        root.host = Hosts.host(hostField.editText);
        root.host.connectToHost();
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.2)
    width: 440
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

            Item {
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Close")
                onClicked: root.close()
            }
            Button {
                readonly property bool connected: root.host !== null && root.host.destination === hostField.editText.trim() && root.host.state !== Host.Disconnected && root.host.state !== Host.Failed

                highlighted: true
                enabled: hostField.editText.trim() !== ""
                text: connected ? qsTr("Disconnect") : qsTr("Connect")
                onClicked: connected ? root.host.disconnectFromHost() : root.connectHost()
            }
        }
    }
}
