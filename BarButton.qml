pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Controls.impl

import Rask.Controls
import Rask.Platform
import Kode

// A button of the bars in the title bar: flat, an SF Symbol and a light
// fill under the pointer (the macOS style draws a bezel).
T.ToolButton {
    id: barButton

    // Shown when the pointer rests on it.
    property string toolTip
    // Not `enabled`: IconImage draws the symbol once, in the state the
    // button had then, so the button stays enabled and this only fades it.
    property bool available: true

    implicitWidth: 28
    implicitHeight: 26
    padding: 0
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    icon.width: 15
    icon.height: 15
    // Counted, not set on the window: leaving one button may come after entering the next.
    onHoveredChanged: TitleBarButtons.hovered = Math.max(0, TitleBarButtons.hovered + (hovered ? 1 : -1))
    Component.onDestruction: {
        if (hovered)
            TitleBarButtons.hovered = Math.max(0, TitleBarButtons.hovered - 1);
    }

    background: Rectangle {
        radius: 6
        color: barButton.hovered && barButton.available ? Qt.alpha(SystemColors.label, barButton.down ? 0.16 : 0.08) : "transparent"
    }

    HelpTag {
        text: barButton.toolTip
    }

    contentItem: IconImage {
        name: barButton.icon.name
        sourceSize.width: barButton.icon.width
        sourceSize.height: barButton.icon.height
        fillMode: Image.Pad
        // Checked (a toggle that is on): in the accent color.
        color: barButton.checked ? SystemColors.controlAccent : SystemColors.label
        opacity: !barButton.available ? 0.35 : (barButton.hovered ? 1 : 0.8)
    }
}
