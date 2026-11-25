import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window

    width: 900
    height: 640
    minimumWidth: 320
    minimumHeight: 200
    visible: true
    title: qsTr("Untitled") + " — Tekst"

    ScrollView {
        anchors.fill: parent

        TextArea {
            id: editor

            focus: true
            wrapMode: TextEdit.Wrap
            font.family: "Menlo"
            font.pointSize: 13
            selectByMouse: true
        }
    }
}
