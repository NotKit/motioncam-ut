import QtQuick 2.12
import QtQuick.Controls 2.5
import Lomiri.Components 1.3
import MotionCam 1.0

Item {
    id: root

    // ── Camera bridge (preview + capture) ─────────────────────────────────────
    CameraBridge {
        id: camera
        anchors.fill: parent

        Component.onCompleted: camera.startCamera()

        onCameraError: function(msg) {
            errorLabel.text = msg
            errorLabel.visible = true
        }
        onRecordingChanged: {
            recordBtn.text = camera.recording ? "■ Stop" : "● Record"
        }
        onExposureChanged: {
            if (camera.isoValue > 0)
                exposureLabel.text = "ISO " + camera.isoValue
        }
    }

    // ── Recording overlay ─────────────────────────────────────────────────────
    RecordingOverlay {
        id: overlay
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: units.gu(6)
        visible: camera.recording
        frameCount: camera.frameCount
    }

    // ── Exposure info ─────────────────────────────────────────────────────────
    Label {
        id: exposureLabel
        anchors { top: parent.top; right: parent.right; margins: units.gu(1) }
        color: "white"
        font.pixelSize: units.gu(1.5)
        visible: !camera.recording && camera.ready
    }

    // ── Error banner ──────────────────────────────────────────────────────────
    Label {
        id: errorLabel
        anchors.centerIn: parent
        color: "red"
        font.pixelSize: units.gu(2)
        visible: false
        wrapMode: Text.WordWrap
        width: parent.width * 0.8
    }

    // ── Bottom controls ───────────────────────────────────────────────────────
    Item {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: units.gu(12)

        Row {
            anchors.centerIn: parent
            spacing: units.gu(3)

            // Manual exposure toggle
            RoundButton {
                id: aeModeBtn
                width: units.gu(6); height: width
                text: "A"
                font.pixelSize: units.gu(2)
                onClicked: {
                    if (manualControls.visible) {
                        manualControls.visible = false
                        camera.setAutoExposure()
                    } else {
                        manualControls.visible = true
                    }
                }
            }

            // Record / stop button
            RoundButton {
                id: recordBtn
                width: units.gu(9); height: width
                text: "● Record"
                font.pixelSize: units.gu(1.8)
                enabled: camera.ready && camera.rawCapable
                onClicked: {
                    if (camera.recording)
                        camera.stopRecording()
                    else
                        camera.startRecording()
                }
            }

            // Torch toggle
            RoundButton {
                id: torchBtn
                property bool torchOn: false
                width: units.gu(6); height: width
                text: "☀"
                font.pixelSize: units.gu(2.5)
                onClicked: {
                    torchOn = !torchOn
                    camera.setTorch(torchOn)
                }
            }
        }
    }

    // ── Manual exposure controls ──────────────────────────────────────────────
    Column {
        id: manualControls
        visible: false
        anchors { bottom: parent.bottom; bottomMargin: units.gu(13); left: parent.left; right: parent.right }
        spacing: units.gu(0.5)
        padding: units.gu(1)

        Label { text: "ISO: " + isoSlider.value.toFixed(0); color: "white"; font.pixelSize: units.gu(1.5) }
        Slider {
            id: isoSlider
            width: parent.width - units.gu(2)
            from: 50; to: 3200; value: 200; stepSize: 50
            onValueChanged: applyManual()
        }

        Label { text: "Shutter: 1/" + (1000 / shutterSlider.value).toFixed(0) + "s"; color: "white"; font.pixelSize: units.gu(1.5) }
        Slider {
            id: shutterSlider
            width: parent.width - units.gu(2)
            from: 1; to: 200; value: 30
            onValueChanged: applyManual()
        }

        function applyManual() {
            camera.setManualExposure(isoSlider.value, shutterSlider.value)
        }
    }

    // ── Touch-to-focus ────────────────────────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: function(mouse) {
            camera.setFocusPoint(mouse.x / width, mouse.y / height)
        }
    }
}
