import QtQuick

Rectangle {
    id: card

    property date time
    property real temperature
    property real wind
    property bool highlighted: false

    width: 96
    height: 132
    radius: 12
    color: highlighted ? "#33e0703b" : "#14ffffff"

    Column {
        anchors.centerIn: parent
        spacing: 6

        Text {
            text: Qt.formatTime(card.time, "HH:mm")
            color: palette.placeholderText
        }
        Text {
            text: card.temperature + "°"
            font.pixelSize: 26
            color: palette.windowText
        }
        Text {
            text: card.wind.toFixed(1) + " m/s"
            color: palette.placeholderText
        }
    }
}
