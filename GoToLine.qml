import QtQuick
import QtQuick.Controls

import Rask.Controls
import Rask.Platform

// A small field for "line" or "line:column", in the style of Xcode's.
Popup {
    id: root

    signal accepted(string target)

    width: 320
    modal: false
    focus: true
    padding: 8
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: 10
        color: root.palette.window
        border.color: Qt.alpha(SystemColors.label, 0.15)
    }

    onOpened: {
        field.clear();
        field.forceActiveFocus();
    }

    contentItem: FilledTextField {
        id: field

        placeholderText: qsTr("Line or line:column")
        Accessible.name: qsTr("Go to line")
        validator: RegularExpressionValidator {
            regularExpression: /\d*(:\d*)?/
        }
        onAccepted: root.accepted(text)
    }
}
