import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window

    required property string station

    width: 720
    height: 480
    visible: true
    title: qsTr("Aurora — %1").arg(station)

    ForecastView {
        anchors.fill: parent
        station: window.station
    }
}
