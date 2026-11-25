pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Rask.Platform

import Tekst

// Below the editor: where the caret is and the syntax, with a menu to pick it.
Item {
    id: root

    required property EditorView editor

    implicitHeight: 26

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Qt.alpha(SystemColors.label, 0.1)
    }

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Line %1, Column %2").arg(root.editor.cursorLine + 1).arg(root.editor.cursorColumn + 1)
        color: SystemColors.secondaryLabel
        font.pixelSize: 11
    }

    Label {
        id: syntaxLabel

        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: root.editor.syntax === "" ? qsTr("Plain Text") : root.editor.syntax
        color: syntaxArea.containsMouse ? SystemColors.label : SystemColors.secondaryLabel
        font.pixelSize: 11

        MouseArea {
            id: syntaxArea

            anchors.fill: parent
            anchors.margins: -6
            hoverEnabled: true
            onClicked: syntaxMenu.popup(syntaxLabel, 0, syntaxLabel.height)
        }
    }

    Menu {
        id: syntaxMenu

        popupType: Popup.Native

        MenuItem {
            text: qsTr("Plain Text")
            checkable: true
            checked: root.editor.syntax === ""
            onTriggered: root.editor.syntax = ""
        }
        MenuSeparator {}

        Instantiator {
            model: Syntaxes.sections

            delegate: Menu {
                id: sectionMenu

                required property string modelData

                title: modelData

                Instantiator {
                    model: Syntaxes.names(sectionMenu.modelData)

                    delegate: MenuItem {
                        required property string modelData

                        text: modelData
                        checkable: true
                        checked: root.editor.syntax === modelData
                        onTriggered: root.editor.syntax = modelData
                    }

                    onObjectAdded: (index, object) => sectionMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sectionMenu.removeItem(object)
                }
            }

            onObjectAdded: (index, object) => syntaxMenu.insertMenu(index + 2, object)
            onObjectRemoved: (index, object) => syntaxMenu.removeMenu(object)
        }
    }
}
