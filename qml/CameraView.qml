import QtQuick 2.12
import QtQuick.Controls 2.5 as QQC2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.12
import QtSensors 5.2
import Lomiri.Components 1.3
import MotionCam 1.0

Item {
    id: root
    property string currentMode: "PHOTO"

    // Black letterbox background — visible when viewfinder doesn't fill window
    Rectangle { anchors.fill: parent; color: "black"; z: -1 }

    // Orientation handling
    // Screen.orientation reports the same value for both landscape directions on
    // Ubuntu Touch (Mir doesn't expose InvertedLandscape separately to apps).
    // Use the hardware OrientationSensor instead — it gives the true physical
    // orientation regardless of what the compositor reports.
    //
    // OrientationReading mapping → deviceRotation → displayRotation in C++:
    //   TopUp   (portrait)            dev=0   display=90  UV set 1
    //   LeftUp  (CW, top→right)       dev=90  display=0   UV set 0
    //   TopDown (inverted portrait)   dev=180 display=270 UV set 3
    //   RightUp (CCW, top→left)       dev=270 display=180 UV set 2
    //   FaceUp/FaceDown               keep current (device flat on table)

    OrientationSensor {
        id: orientSensor
        active: true
        onReadingChanged: {
            switch (reading.orientation) {
                case OrientationReading.TopUp:   camera.setDeviceRotation(0);   break
                case OrientationReading.LeftUp:  camera.setDeviceRotation(90);  break
                case OrientationReading.TopDown: camera.setDeviceRotation(180); break
                case OrientationReading.RightUp: camera.setDeviceRotation(270); break
                default: break  // FaceUp / FaceDown — keep previous rotation
            }
        }
    }

    Component.onCompleted: {
        // Apply the sensor's current reading immediately so the first frame
        // is rendered with the correct rotation before any change event fires.
        var o = orientSensor.reading ? orientSensor.reading.orientation
                                     : OrientationReading.TopUp
        switch (o) {
            case OrientationReading.LeftUp:  camera.setDeviceRotation(90);  break
            case OrientationReading.TopDown: camera.setDeviceRotation(180); break
            case OrientationReading.RightUp: camera.setDeviceRotation(270); break
            default:                         camera.setDeviceRotation(0);   break
        }
    }

    // Camera bridge
    CameraBridge {
        id: camera
        anchors.centerIn: parent
        width:  Math.min(parent.width, parent.height * previewAspectRatio)
        height: width / previewAspectRatio

        Component.onCompleted: camera.startCamera()

        onCameraError: function(msg) {
            errorLabel.text = msg
            errorLabel.visible = true
        }
        onRecordingChanged: {}
        onPhotoSaved: function(path) {
            flashOverlay.opacity = 1
            flashTimer.restart()
            savedLabel.text = path.split("/").pop()
            savedLabel.visible = true
            savedLabelTimer.restart()
        }
        onRecordingSaved: function(path) {
            savedLabel.text = path.split("/").pop()
            savedLabel.visible = true
            savedLabelTimer.restart()
        }
    }

    // Shutter flash
    Rectangle {
        id: flashOverlay
        anchors.fill: parent
        color: "white"
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 80 } }
        Timer {
            id: flashTimer
            interval: 120
            onTriggered: flashOverlay.opacity = 0
        }
    }

    // Mode strip
    Row {
        id: modeStrip
        anchors { bottom: controlBar.top; bottomMargin: units.gu(1.5); horizontalCenter: parent.horizontalCenter }
        spacing: units.gu(3)
        visible: !camera.recording

        Repeater {
            model: ["VIDEO", "PHOTO", "RAW"]
            Label {
                text: modelData
                color: modelData === currentMode ? "#FFD700" : "white"
                font.pixelSize: units.gu(1.5)
                font.bold: modelData === currentMode
                style: Text.Outline; styleColor: "#80000000"
                MouseArea {
                    anchors.fill: parent
                    onClicked: currentMode = modelData
                }
            }
        }
    }

    // Bottom control bar
    Item {
        id: controlBar
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: units.gu(14)

        // Thumbnail placeholder (left)
        Rectangle {
            id: thumbnailBtn
            width: units.gu(6.5); height: width
            radius: units.gu(0.8)
            color: "#30ffffff"
            border.color: "white"; border.width: 1
            anchors { left: parent.left; leftMargin: units.gu(3); verticalCenter: parent.verticalCenter }
            Label {
                anchors.centerIn: parent
                text: "⊞"
                font.pixelSize: units.gu(3)
                color: "white"
            }
        }

        // Shutter button (center)
        Item {
            anchors.centerIn: parent
            width: units.gu(10); height: width

            // Outer ring
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.color: "white"
                border.width: units.gu(0.3)
            }

            // Inner fill
            Rectangle {
                id: shutterInner
                anchors.centerIn: parent
                width: parent.width - units.gu(1.2)
                height: width
                radius: width / 2
                color: currentMode === "VIDEO" ? "#E53935"
                     : currentMode === "RAW"   ? "#29B6F6"
                     : "white"

                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on scale { NumberAnimation { duration: 80 } }

                MouseArea {
                    anchors.fill: parent
                    onPressed:  shutterInner.scale = 0.88
                    onReleased: {
                        shutterInner.scale = 1.0
                        if (!camera.ready) return
                        if (currentMode === "VIDEO") {
                            if (camera.recording) camera.stopRecording()
                            else                   camera.startRecording()
                        } else {
                            camera.capturePhoto()
                        }
                    }
                }
            }

            // Video recording indicator: stop square overlay
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.35; height: width
                radius: units.gu(0.4)
                color: "white"
                visible: currentMode === "VIDEO" && camera.recording
            }
        }

        // Camera switch button (right)
        QQC2.RoundButton {
            id: switchBtn
            width: units.gu(6.5); height: width
            anchors { right: parent.right; rightMargin: units.gu(3); verticalCenter: parent.verticalCenter }
            text: "⟳"
            font.pixelSize: units.gu(3)
            background: Rectangle {
                radius: switchBtn.width / 2
                color: "#30ffffff"
                border.color: "white"; border.width: 1
            }
            contentItem: Label {
                text: switchBtn.text
                font: switchBtn.font
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: {} // future: switch front/back
        }
    }

    // Manual exposure panel
    Column {
        id: manualControls
        visible: false
        anchors {
            bottom: modeStrip.top; bottomMargin: units.gu(1)
            left: parent.left; right: parent.right
        }
        spacing: units.gu(0.5)
        padding: units.gu(1)

        Label { text: "ISO: " + isoSlider.value.toFixed(0); color: "white"; font.pixelSize: units.gu(1.5)
                style: Text.Outline; styleColor: "#80000000" }
        QQC2.Slider {
            id: isoSlider
            width: parent.width - units.gu(2)
            from: 50; to: 3200; value: 200; stepSize: 50
            onValueChanged: manualControls.applyManual()
        }

        Label { text: "Shutter: 1/" + Math.round(1000 / shutterSlider.value) + "s"
                color: "white"; font.pixelSize: units.gu(1.5)
                style: Text.Outline; styleColor: "#80000000" }
        QQC2.Slider {
            id: shutterSlider
            width: parent.width - units.gu(2)
            from: 1; to: 200; value: 30
            onValueChanged: manualControls.applyManual()
        }

        function applyManual() {
            camera.setManualExposure(isoSlider.value, shutterSlider.value)
        }
    }

    // Top-right toolbar removed for now — the old "A/M" exposure toggle, ISO
    // readout and (non-functional) torch button all lived here. Manual exposure
    // and torch will return in the slide-down panel; see parity tracker.

    // Recording timer (video mode)
    Row {
        anchors { top: parent.top; left: parent.left; margins: units.gu(1.5) }
        spacing: units.gu(0.8)
        visible: camera.recording

        Rectangle { width: units.gu(1); height: width; radius: width/2; color: "#E53935"
                    anchors.verticalCenter: parent.verticalCenter }
        Label { text: "REC"; color: "white"; font.pixelSize: units.gu(1.8); font.bold: true
                style: Text.Outline; styleColor: "#80000000" }
    }

    // Save confirmation
    Rectangle {
        id: savedBanner
        anchors { bottom: modeStrip.top; bottomMargin: units.gu(2); horizontalCenter: parent.horizontalCenter }
        color: "#CC000000"
        radius: units.gu(1)
        width: savedLabel.width + units.gu(3)
        height: savedLabel.height + units.gu(1.5)
        visible: savedLabel.visible

        Label {
            id: savedLabel
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: units.gu(1.5)
            visible: false
            Timer { id: savedLabelTimer; interval: 3000; onTriggered: savedLabel.visible = false }
        }
    }

    // Error label
    Label {
        id: errorLabel
        anchors.centerIn: parent
        color: "#FF5252"
        font.pixelSize: units.gu(1.8)
        visible: false
        wrapMode: Text.WordWrap
        width: parent.width * 0.8
        style: Text.Outline; styleColor: "#80000000"
    }

    // Touch-to-focus
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: function(mouse) {
            camera.setFocusPoint(mouse.x / width, mouse.y / height)
            focusRing.x = mouse.x - focusRing.width / 2
            focusRing.y = mouse.y - focusRing.height / 2
            focusRing.opacity = 1
            focusRingTimer.restart()
        }
    }

    // Focus ring
    Rectangle {
        id: focusRing
        width: units.gu(6); height: width
        radius: width / 2
        color: "transparent"
        border.color: "#FFD700"; border.width: units.gu(0.2)
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Timer { id: focusRingTimer; interval: 1500; onTriggered: focusRing.opacity = 0 }
    }
}
