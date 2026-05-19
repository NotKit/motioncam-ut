import QtQuick 2.12
import QtQuick.Window 2.12
import Lomiri.Components 1.3

MainView {
    id: root
    objectName: "mainView"
    applicationName: "motioncam.thekit"

    width:  units.gu(45)
    height: units.gu(75)

    backgroundColor: "black"

    Page {
        anchors.fill: parent
        header: PageHeader {
            id: pageHeader
            title: "MotionCam"
            opacity: 0
        }

        CameraView {
            anchors.fill: parent
        }
    }
}
