import QtQuick
import QtQuick.Layouts
import Aurora

Item {
    id: root

    property alias station: forecast.station
    readonly property bool falling: forecast.trend < -0.2

    Forecast {
        id: forecast
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: root.station.toUpperCase()
            font.pixelSize: 28
            font.weight: Font.DemiBold
            color: palette.windowText
        }

        Text {
            text: root.falling ? qsTr("Wind ahead") : qsTr("Steady")
            color: root.falling ? "#e0703b" : palette.mid
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            orientation: ListView.Horizontal
            spacing: 12
            model: forecast.hours

            delegate: HourCard {
                required property var modelData

                time: modelData.time
                temperature: modelData.temperature
                wind: modelData.wind
                highlighted: wind > 8
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
