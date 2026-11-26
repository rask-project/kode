import QtQuick

// A SplitView handle: a 1 px line that takes the pointer a few pixels
// either side, so it is easy to grab.
Item {
    id: handle

    property alias color: line.color

    implicitWidth: 1
    implicitHeight: 1
    containmentMask: Item {
        x: (handle.width - width) / 2
        y: (handle.height - height) / 2
        width: Math.max(handle.width, 9)
        height: Math.max(handle.height, 9)
    }

    // The cursor over all of that: SplitView sets it on the line's pixel only.
    HoverHandler {
        margin: 4
        cursorShape: handle.width < handle.height ? Qt.SplitHCursor : Qt.SplitVCursor
    }

    Rectangle {
        id: line

        anchors.fill: parent
    }
}
