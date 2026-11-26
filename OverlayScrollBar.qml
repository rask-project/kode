import QtQuick
import QtQuick.Controls.Basic as Basic

import Rask.Platform

// Basic's scroll bar: no track, only a handle that shows while scrolling
// (the macOS style draws a track and is not meant to be customized). The
// editor's and the sidebar's.
Basic.ScrollBar {
    id: bar

    contentItem: Rectangle {
        implicitWidth: 7
        implicitHeight: 7
        radius: 3.5
        color: Qt.alpha(SystemColors.label, bar.pressed ? 0.5 : 0.3)
        opacity: bar.active && bar.size < 1.0 ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation {
                duration: 200
            }
        }
    }
}
