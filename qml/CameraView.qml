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
    // The hardware sensor fires before Mir resizes the window, which would cause
    // the CameraBridge to adopt the new aspect ratio while the window is still
    // the old shape — producing a visible flicker.
    //
    // Fix: store the sensor reading as a pending rotation and only commit it
    // once the window dimensions confirm the system has finished rotating
    // (portrait↔landscape transitions always come with a window resize).
    // For same-size transitions (portrait↔inverted-portrait, left↔right
    // landscape) applyIfReady() fires immediately since the aspect ratio already
    // matches and no window resize will follow.

    property int _pendingRot: 0

    OrientationSensor {
        id: orientSensor
        active: true
        onReadingChanged: {
            var rot
            switch (reading.orientation) {
                case OrientationReading.TopUp:   rot = 0;   break
                case OrientationReading.LeftUp:  rot = 90;  break
                case OrientationReading.TopDown: rot = 180; break
                case OrientationReading.RightUp: rot = 270; break
                default: return  // FaceUp / FaceDown — ignore
            }
            root._pendingRot = rot
            // For same-axis transitions (portrait↔inverted, left↔right landscape)
            // no window resize follows, so schedule directly.
            Qt.callLater(root.applyIfReady)
        }
    }

    // Qt.callLater deduplicates: even though onWidthChanged and onHeightChanged
    // fire separately, applyIfReady() runs only once per event-loop tick, after
    // both dimensions are settled. This prevents the intermediate square-window
    // state from satisfying the portrait/landscape check prematurely.
    onWidthChanged:  Qt.callLater(root.applyIfReady)
    onHeightChanged: Qt.callLater(root.applyIfReady)

    function applyIfReady() {
        var pendingPortrait = (_pendingRot === 0 || _pendingRot === 180)
        var windowPortrait  = (root.height >= root.width)
        if (pendingPortrait === windowPortrait)
            camera.setDeviceRotation(_pendingRot)
    }

    Component.onCompleted: {
        var o = orientSensor.reading ? orientSensor.reading.orientation
                                     : OrientationReading.TopUp
        switch (o) {
            case OrientationReading.LeftUp:  _pendingRot = 90;  break
            case OrientationReading.TopDown: _pendingRot = 180; break
            case OrientationReading.RightUp: _pendingRot = 270; break
            default:                         _pendingRot = 0;   break
        }
        camera.setDeviceRotation(_pendingRot)
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
        onProcessingStarted: { processingOverlay.visible = true }
        onProcessingStopped: { processingOverlay.visible = false }
        onProcessingProgress: function(percent) { processingBar.value = percent }
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

    // Processing overlay
    Rectangle {
        id: processingOverlay
        anchors.centerIn: parent
        width: units.gu(22); height: units.gu(6)
        radius: units.gu(1)
        color: "#CC000000"
        visible: false

        Column {
            anchors.centerIn: parent
            spacing: units.gu(0.5)
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Processing…"
                color: "white"
                font.pixelSize: units.gu(1.6)
            }
            QQC2.ProgressBar {
                id: processingBar
                anchors.horizontalCenter: parent.horizontalCenter
                width: units.gu(18)
                from: 0; to: 100; value: 0
            }
        }
    }

    // Quick-settings strip (EV compensation + locks)
    // Two pill-shaped panels above the mode strip, matching the Android layout.
    Row {
        id: quickStrip
        anchors {
            bottom: modeStrip.top; bottomMargin: units.gu(1.5)
            horizontalCenter: parent.horizontalCenter
        }
        spacing: units.gu(1.5)
        visible: !camera.recording && !settingsPanel.open

        // Left panel: EV compensation ─────────────────────────────────────────
        // evIndex is stored as integer steps of 1/3 EV across a ±2 EV range
        // (12 steps total). The bridge call expects 0.0–1.0, where 0.5 = neutral.
        Rectangle {
            id: evPanel
            property int evIndex: 0          // -6 .. +6 in 1/3 EV steps
            property real evValue: evIndex / 3.0
            width: units.gu(22); height: units.gu(7)
            radius: units.gu(1)
            color: "#A0202832"

            function evInc() {
                if (evIndex < 6) { evIndex++; camera.setExposureCompensation((evIndex + 6) / 12.0) }
            }
            function evDec() {
                if (evIndex > -6) { evIndex--; camera.setExposureCompensation((evIndex + 6) / 12.0) }
            }

            // +/- buttons at corners (top row)
            Label {
                anchors { top: parent.top; left: parent.left; margins: units.gu(0.8) }
                text: "+"; color: "white"; font.pixelSize: units.gu(2); font.bold: true
                MouseArea { anchors.fill: parent; anchors.margins: -units.gu(0.6); onClicked: evPanel.evInc() }
            }
            Label {
                anchors { top: parent.top; right: parent.right; margins: units.gu(0.8) }
                text: "−"; color: "white"; font.pixelSize: units.gu(2); font.bold: true
                MouseArea { anchors.fill: parent; anchors.margins: -units.gu(0.6); onClicked: evPanel.evDec() }
            }

            // Centred EV readout
            Label {
                anchors.centerIn: parent
                text: (evPanel.evValue >= 0 ? "+" : "") + evPanel.evValue.toFixed(2) + " EV"
                color: "white"
                font.pixelSize: units.gu(1.8)
                font.bold: true
            }

            // Sun/moon glyphs (bottom row)
            Label {
                anchors { bottom: parent.bottom; left: parent.left; margins: units.gu(0.8) }
                text: "☀"; color: "#cccccc"; font.pixelSize: units.gu(1.6)
            }
            Label {
                anchors { bottom: parent.bottom; right: parent.right; margins: units.gu(0.8) }
                text: "☾"; color: "#cccccc"; font.pixelSize: units.gu(1.6)
            }
        }

        // Right panel: AE / AF / AWB lock chips ───────────────────────────────
        Rectangle {
            id: lockPanel
            property bool aeLocked:  false
            property bool afLocked:  false
            property bool awbLocked: false
            width: units.gu(24); height: units.gu(7)
            radius: units.gu(1)
            color: "#A0202832"

            Row {
                anchors.centerIn: parent
                spacing: units.gu(1)

                Repeater {
                    model: [
                        { label: "AE",  prop: "aeLocked"  },
                        { label: "AF",  prop: "afLocked"  },
                        { label: "AWB", prop: "awbLocked" },
                    ]
                    // Lock chip — matches Android's open/closed-padlock + text-colour cue.
                    // Locked: 🔒 + gold text. Unlocked: 🔓 + white text. Background
                    // stays neutral; the lock metaphor is carried by the padlock glyph,
                    // not the chip colour (which previously read as "feature enabled").
                    Rectangle {
                        property var cfg: modelData
                        property bool active: lockPanel[cfg.prop]
                        width: units.gu(7); height: units.gu(4.5)
                        radius: units.gu(0.6)
                        color: "#30ffffff"
                        border.color: "#80ffffff"; border.width: 1
                        Row {
                            anchors.centerIn: parent
                            spacing: units.gu(0.4)
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: active ? "🔒" : "🔓"
                                color: active ? "#FFD700" : "#cccccc"
                                font.pixelSize: units.gu(1.4)
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: cfg.label
                                color: active ? "#FFD700" : "white"
                                font.pixelSize: units.gu(1.5)
                                font.bold: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                lockPanel[cfg.prop] = !active
                                if (cfg.prop === "aeLocked")  camera.setAELock(!active)
                                if (cfg.prop === "afLocked")  camera.setFocusLock(!active)
                                if (cfg.prop === "awbLocked") camera.setAWBLock(!active)
                            }
                        }
                    }
                }
            }
        }
    }

    // Mode strip
    Row {
        id: modeStrip
        anchors { bottom: controlBar.top; bottomMargin: units.gu(1.5); horizontalCenter: parent.horizontalCenter }
        spacing: units.gu(3)
        visible: !camera.recording

        Repeater {
            model: ["VIDEO", "PHOTO", "BURST"]
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

        // Thumbnail (left) — shows last captured photo; opens it on tap
        Rectangle {
            id: thumbnailBtn
            width: units.gu(6.5); height: width
            radius: units.gu(0.8)
            color: "#30ffffff"
            border.color: "white"; border.width: 1
            clip: true
            anchors { left: parent.left; leftMargin: units.gu(3); verticalCenter: parent.verticalCenter }

            Image {
                id: thumbImage
                anchors.fill: parent
                source: camera.lastPhotoPath.length > 0 ? ("file://" + camera.lastPhotoPath) : ""
                fillMode: Image.PreserveAspectCrop
                visible: status === Image.Ready
            }
            Label {
                anchors.centerIn: parent
                text: "⊞"
                font.pixelSize: units.gu(3)
                color: "white"
                visible: thumbImage.status !== Image.Ready
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (camera.lastPhotoPath.length > 0)
                        Qt.openUrlExternally("file://" + camera.lastPhotoPath)
                }
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
                color: currentMode === "VIDEO"  ? "#E53935"
                     : currentMode === "BURST" ? "#26C6DA"
                     : "white"

                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on scale { NumberAnimation { duration: 80 } }

                MouseArea {
                    anchors.fill: parent
                    onPressed: {
                        shutterInner.scale = 0.88
                        // Queue the HDR underexposed frame while the user is still
                        // pressing — capturePhoto on release will then save with it.
                        if (camera.ready && currentMode === "PHOTO")
                            camera.prepareHdrCapture()
                    }
                    onReleased: {
                        shutterInner.scale = 1.0
                        if (!camera.ready) return
                        // Close the settings panel — current values are read into
                        // the capture call below, then the user sees the preview.
                        settingsPanel.open = false
                        if (currentMode === "VIDEO") {
                            if (camera.recording) camera.stopRecording()
                            else                   camera.startRecording()
                        } else if (currentMode === "BURST") {
                            camera.acquireBurstFrames()
                        } else {
                            camera.capturePhoto("", root._buildPhotoSettings())
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

        // Camera switch button (right) — only shown when a front camera exists
        QQC2.RoundButton {
            id: switchBtn
            width: units.gu(6.5); height: width
            visible: camera.hasFrontCamera
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
            onClicked: camera.switchCamera()
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

    // Quick-settings slide-down panel
    // Holds JPEG/DNG/NR toggles + Camera Profile sliders. Settings live as
    // properties on settingsPanel for now; wiring into capturePhoto()/saveBurstFrame()
    // is a follow-up.
    Item {
        id: settingsPanel
        property bool open: false
        property bool saveJpeg: true
        property bool saveDng: false
        property bool denoiseEnabled: true
        property bool saveBurstAsVideo: false
        // Stored as raw PostProcessSettings values (defaults match the C++ ctor).
        property real contrast:    0.5
        property real saturation:  1.05
        property real tintOffset:  0     // added to estimated tint at save time
        property real warmthOffset: 0    // K, added to estimated temperature

        // Sized to fit content (chevron room + Column + bottom padding); slid
        // off-screen by binding y. anchors.top must NOT be set or it overrides
        // the y binding and the panel stays "open". Height is capped to the
        // area above the mode strip so the panel can't push past it.
        width: parent.width
        height: Math.min(panelColumn.height + units.gu(8), modeStrip.y - units.gu(1))
        y: open ? 0 : -height
        z: 15
        clip: true
        Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

        // Tap-blocker so background touches don't pass through to focus etc.
        MouseArea { anchors.fill: parent }

        Rectangle { anchors.fill: parent; color: "#E0101418" }

        Item {
            anchors {
                top: parent.top; topMargin: units.gu(6)   // leave room for the chevron
                left: parent.left; right: parent.right
                bottom: parent.bottom
                margins: units.gu(2)
            }

            Column {
                id: panelColumn
                width: parent.width
                spacing: units.gu(1)

                // ── Save options card ────────────────────────────────────
                Rectangle {
                    width: parent.width
                    color: "#1c2330"
                    radius: units.gu(1)
                    height: saveOpts.height + units.gu(2)

                    Column {
                        id: saveOpts
                        anchors {
                            left: parent.left; right: parent.right
                            verticalCenter: parent.verticalCenter
                            leftMargin: units.gu(1.5); rightMargin: units.gu(1.5)
                        }
                        spacing: units.gu(1)

                        Repeater {
                            model: [
                                { title: "JPEG",            desc: "Save JPEG",                         key: "saveJpeg"          },
                                { title: "DNG",             desc: "Save DNG",                          key: "saveDng"           },
                                { title: "Noise reduction", desc: "Apply noise reduction to DNG",      key: "denoiseEnabled"    },
                                { title: "Save burst as video",
                                  desc: "Save burst capture photos as a short MCRAW container video", key: "saveBurstAsVideo"  },
                            ]
                            Row {
                                width: saveOpts.width
                                spacing: units.gu(1)
                                Column {
                                    width: parent.width - sw.width - units.gu(1)
                                    anchors.verticalCenter: parent.verticalCenter
                                    Label {
                                        text: modelData.title
                                        color: "white"
                                        font.pixelSize: units.gu(1.7)
                                        font.bold: true
                                    }
                                    Label {
                                        text: modelData.desc
                                        color: "#9aa4b2"
                                        font.pixelSize: units.gu(1.2)
                                        wrapMode: Text.WordWrap
                                        width: parent.width
                                    }
                                }
                                QQC2.Switch {
                                    id: sw
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: settingsPanel[modelData.key]
                                    onToggled: settingsPanel[modelData.key] = checked
                                }
                            }
                        }
                    }
                }

                // ── Camera Profile section ───────────────────────────────
                Item {
                    width: settingsPanel.width - units.gu(4)
                    height: units.gu(4)
                    Label {
                        anchors {
                            left: parent.left; leftMargin: units.gu(0.5)
                            verticalCenter: parent.verticalCenter
                        }
                        text: "Camera Profile"
                        color: "white"
                        font.pixelSize: units.gu(2)
                        font.bold: true
                    }
                }

                Repeater {
                    model: [
                        { key: "contrast",      label: "Contrast",   from: 0,     to: 1,    def: 0.5,  scale: 100 },
                        { key: "saturation",    label: "Saturation", from: 0,     to: 2,    def: 1.05, scale: 50  },
                        { key: "tintOffset",    label: "Tint",       from: -50,   to: 50,   def: 0,    scale: 1   },
                        { key: "warmthOffset",  label: "Warmth",     from: -1500, to: 1500, def: 0,    scale: 1   },
                    ]
                    Column {
                        width: settingsPanel.width - units.gu(4)
                        spacing: units.gu(0.2)
                        property var cfg: modelData
                        Row {
                            width: parent.width
                            Label {
                                text: cfg.label
                                color: "white"
                                font.pixelSize: units.gu(1.6)
                                width: parent.width * 0.6
                            }
                            Label {
                                text: cfg.key === "warmthOffset"
                                    ? (sl.value >= 0 ? "+" : "") + Math.round(sl.value) + "K"
                                    : cfg.key === "tintOffset"
                                        ? (sl.value >= 0 ? "+" : "") + Math.round(sl.value)
                                        : Math.round(sl.value * cfg.scale) + "%"
                                color: "#9aa4b2"
                                font.pixelSize: units.gu(1.4)
                                width: parent.width * 0.4
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                        QQC2.Slider {
                            id: sl
                            width: parent.width
                            from: cfg.from; to: cfg.to
                            value: settingsPanel[cfg.key]
                            onMoved: settingsPanel[cfg.key] = value
                        }
                    }
                }
            }
        }
    }

    // ── Chevron toggle (top center) ───────────────────────────────────────────
    Rectangle {
        id: chevronBtn
        anchors { top: parent.top; topMargin: units.gu(2.5); horizontalCenter: parent.horizontalCenter }
        width: units.gu(4); height: units.gu(2.4); radius: units.gu(1.2)
        color: "#80000000"
        border.color: "#80ffffff"; border.width: 1
        z: 20

        Label {
            anchors.centerIn: parent
            text: "⌄"
            color: "white"
            font.pixelSize: units.gu(2)
            font.bold: true
            rotation: settingsPanel.open ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 200 } }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: settingsPanel.open = !settingsPanel.open
        }
    }

    // Touch-to-focus
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: function(mouse) {
            // Settings panel covers preview — don't focus through it.
            if (settingsPanel.open) return
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

    // ── Burst post-process overlay ────────────────────────────────────────────
    PostProcessView {
        id: postProcessView
        anchors.fill: parent
        visible: false
        z: 10

        onClosed: {
            visible = false
            frames = []
        }
    }

    Connections {
        target: camera
        onBurstFramesReady: function(frameList) {
            postProcessView.onFramesReady(frameList)
            postProcessView.visible = true
        }
    }

    // Serialise the quick-settings panel into a settings JSON for capturePhoto().
    // Mirrors the Android CameraActivity.capture() flow: contrast, saturation,
    // dng and spatialDenoiseLevel are direct PostProcessSettings overrides;
    // temperatureOffset and tintOffset are bridge-only fields — the C++ side
    // runs estimateSettings() on the latest ZSL buffer and adds the offset
    // to get the absolute temperature/tint to pass to libMotionCam.
    function _buildPhotoSettings() {
        var s = {
            contrast:            settingsPanel.contrast,
            saturation:          settingsPanel.saturation,
            spatialDenoiseLevel: settingsPanel.denoiseEnabled ? -1 : 0,
            dng:                 settingsPanel.saveDng,
            saveJpeg:            settingsPanel.saveJpeg,
            temperatureOffset:   settingsPanel.warmthOffset,
            tintOffset:          settingsPanel.tintOffset
        }
        return JSON.stringify(s)
    }
}
