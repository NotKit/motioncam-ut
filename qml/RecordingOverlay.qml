import QtQuick 2.12
import Lomiri.Components 1.3

Item {
    id: root
    property int frameCount: 0

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    Row {
        anchors { left: parent.left; leftMargin: units.gu(1); verticalCenter: parent.verticalCenter }
        spacing: units.gu(2)

        // Recording dot
        Rectangle {
            width: units.gu(1.5); height: width
            radius: width / 2
            color: "red"
            anchors.verticalCenter: parent.verticalCenter
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0.2; duration: 500 }
                NumberAnimation { to: 1.0; duration: 500 }
            }
        }

        // Elapsed timer
        Label {
            id: timerLabel
            color: "white"
            font.pixelSize: units.gu(2)
            property int elapsed: 0
            text: {
                var h = Math.floor(elapsed / 3600)
                var m = Math.floor((elapsed % 3600) / 60)
                var s = elapsed % 60
                return (h > 0 ? (h < 10 ? "0" + h : h) + ":" : "")
                    + (m < 10 ? "0" + m : m) + ":"
                    + (s < 10 ? "0" + s : s)
            }
            Timer {
                interval: 1000; repeat: true; running: root.visible
                onTriggered: timerLabel.elapsed++
            }
            onVisibleChanged: if (!visible) elapsed = 0
        }

        // Frame counter
        Label {
            color: "#aaffffff"
            font.pixelSize: units.gu(1.6)
            anchors.verticalCenter: parent.verticalCenter
            text: root.frameCount + " frames"
        }
    }
}
