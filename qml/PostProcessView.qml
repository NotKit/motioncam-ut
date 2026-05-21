import QtQuick 2.12
import QtQuick.Controls 2.5 as QQC2
import Lomiri.Components 1.3

// Full-screen post-process overlay matching the original motioncam Android UI:
//   - Large preview (top, full width)
//   - Filmstrip with time offsets (below preview)
//   - Grouped adjustment sliders (scrollable)
//   - Teal circular FAB to save, back arrow to cancel
Item {
    id: root

    signal closed()

    property var    frames: []
    property int    selectedIndex: 0
    property string settingsJson: "{}"
    property int    previewToken: 0
    property int    thumbnailVersion: 0
    // Signal broadcast when a preset replaces _s wholesale so sliders re-read values.
    signal settingsReset()

    // Defaults mirror PostProcessSettings C++ constructor so that the first
    // serialised JSON matches what empty-JSON {} would produce.
    property var _s: ({
        "exposure":    0.0,
        "shadows":     1.0,
        "contrast":    0.5,
        "whitePoint":  1.0,
        "blacks":      0.0,
        "saturation":  1.05,
        "temperature": 5000,
        "tint":        0,
        "sharpen0":    2.0,
        "sharpen1":    2.0,
        "pop":         1.25,
    })

    function _timeOffset(idx) {
        if (frames.length === 0 || idx >= frames.length) return "0.00 s"
        var refTs = frames[selectedIndex].timestamp
        var delta = (frames[idx].timestamp - refTs) / 1e9
        return (delta > 0 ? "+" : "") + delta.toFixed(2) + " s"
    }

    function _requestPreview() {
        if (frames.length === 0) return
        camera.requestBurstPreview(frames[selectedIndex].timestamp, root.settingsJson)
    }

    function _formatValue(v, fmt) {
        if (fmt === "ev")     return (v >= 0 ? "+" : "") + v.toFixed(2) + " EV"
        if (fmt === "pct")    return Math.round(v * 100) + "%"
        if (fmt === "pct2")   return Math.round(v / 0.05 * 100)
        if (fmt === "kelvin") return Math.round(v) + "k"
        if (fmt === "tint")   return (v >= 0 ? "+" : "") + Math.round(v)
        if (fmt === "num1")   return v.toFixed(1)
        return v.toFixed(2)
    }

    function _applyPreset(name) {
        if (name === "auto") {
            root._s = { "exposure": 0.0, "shadows": 1.0, "contrast": 0.5,
                "whitePoint": 1.0, "blacks": 0.0, "saturation": 1.05,
                "temperature": 5000, "tint": 0, "sharpen0": 2.0, "sharpen1": 2.0, "pop": 1.25 }
        } else {
            root._s = { "exposure": 0.0, "shadows": 2.0, "contrast": 0.1,
                "whitePoint": 1.0, "blacks": 0.0, "saturation": 0.7,
                "temperature": 5000, "tint": 0, "sharpen0": 1.0, "sharpen1": 1.0, "pop": 1.0 }
        }
        root.settingsReset()
        previewDebounce.restart()
    }

    Rectangle { anchors.fill: parent; color: "black" }

    // ── Large preview (top, full width) ──────────────────────────────────────
    Item {
        id: previewArea
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Math.min(parent.width * 3 / 4, parent.height * 0.50)

        Image {
            id: previewImg
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            source: previewToken > 0 ? "image://burst/preview?v=" + previewToken : ""
            asynchronous: true
            cache: false
        }

        QQC2.BusyIndicator {
            anchors.centerIn: parent
            running: previewImg.status === Image.Loading
        }

        // Back / cancel button (top-left)
        Rectangle {
            anchors { top: parent.top; left: parent.left; margins: units.gu(1) }
            width: units.gu(4.5); height: width; radius: width / 2
            color: "#88000000"
            Label {
                anchors.centerIn: parent
                text: "‹"
                color: "white"
                font.pixelSize: units.gu(2.8)
            }
            MouseArea {
                anchors.fill: parent
                onClicked: { camera.releaseBurstFrames(); root.closed() }
            }
        }
    }

    // ── Filmstrip (below preview) ─────────────────────────────────────────────
    ListView {
        id: filmstrip
        anchors {
            top: previewArea.bottom; topMargin: units.gu(0.5)
            left: parent.left; right: parent.right
        }
        height: units.gu(13)
        orientation: ListView.Horizontal
        spacing: units.gu(0.5)
        leftMargin: units.gu(0.5)
        rightMargin: units.gu(0.5)
        clip: true
        model: root.frames

        delegate: Item {
            width: units.gu(11); height: filmstrip.height
            property var frame: modelData
            property bool isSelected: index === root.selectedIndex

            Column {
                anchors.fill: parent
                spacing: units.gu(0.2)

                Item {
                    width: parent.width
                    height: parent.height - timeLabel.height - units.gu(0.2)

                    Rectangle {
                        anchors.fill: parent
                        color: "#22ffffff"
                        border.color: isSelected ? "white" : "transparent"
                        border.width: units.gu(0.3)
                        radius: units.gu(0.4)
                        clip: true

                        Image {
                            id: thumbImg
                            anchors { fill: parent; margins: units.gu(0.2) }
                            source: root.thumbnailVersion > 0
                                    ? "image://burst/thumb_" + frame.timestamp
                                      + "?v=" + root.thumbnailVersion
                                    : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: false
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: units.gu(0.2)
                            visible: thumbImg.status !== Image.Ready
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "ISO " + frame.iso
                                color: "white"
                                font.pixelSize: units.gu(1.2)
                            }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "1/" + Math.round(1e9 / Math.max(1, frame.exposureNs)) + "s"
                                color: "#aaaaaa"
                                font.pixelSize: units.gu(1.0)
                            }
                        }

                        Rectangle {
                            anchors { top: parent.top; right: parent.right; margins: units.gu(0.3) }
                            width: units.gu(0.8); height: width; radius: width / 2
                            color: "#4CAF50"
                            visible: frame.sharpest === true
                        }
                    }
                }

                Label {
                    id: timeLabel
                    width: parent.width
                    text: root._timeOffset(index)
                    color: isSelected ? "white" : "#aaaaaa"
                    font.pixelSize: units.gu(1.2)
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: { root.selectedIndex = index; root._requestPreview() }
            }

            Component.onCompleted: camera.requestBurstThumbnail(frame.timestamp)
        }
    }

    // ── Settings panel (grouped, scrollable) ──────────────────────────────────
    Rectangle {
        id: settingsPanel
        anchors {
            top: filmstrip.bottom; topMargin: units.gu(0.5)
            bottom: parent.bottom
            left: parent.left; right: parent.right
        }
        color: "#DD000000"

        Flickable {
            anchors.fill: parent
            contentHeight: settingsCol.implicitHeight + units.gu(9)
            clip: true

            Column {
                id: settingsCol
                width: parent.width
                spacing: 0

                // ── Light ─────────────────────────────────────────────────────
                Item { width: parent.width; height: units.gu(4)
                    Label { anchors.centerIn: parent; text: "Light"; color: "white"; font.bold: true; font.pixelSize: units.gu(1.8) } }

                Repeater {
                    model: [
                        { key: "exposure",   label: "Exposure",    from: -4.0, to: 4.0,   def: 0.0,  fmt: "ev"   },
                        { key: "shadows",    label: "Shadows",     from: 0.01, to: 16.0,  def: 1.0,  fmt: "num2" },
                        { key: "contrast",   label: "Contrast",    from: 0.0,  to: 1.0,   def: 0.5,  fmt: "pct"  },
                        { key: "whitePoint", label: "White Point", from: 0.1,  to: 2.0,   def: 1.0,  fmt: "num2" },
                        { key: "blacks",     label: "Black Point", from: 0.0,  to: 0.05,  def: 0.0,  fmt: "pct2" },
                    ]
                    delegate: Column {
                        width: settingsCol.width
                        leftPadding: units.gu(1); rightPadding: units.gu(1)
                        bottomPadding: units.gu(0.8); spacing: units.gu(0.1)
                        property var cfg: modelData
                        property real displayValue: cfg.key in root._s ? root._s[cfg.key] : cfg.def
                        Connections { target: root; onSettingsReset: { sl.value = cfg.key in root._s ? root._s[cfg.key] : cfg.def } }
                        Row {
                            width: parent.width - units.gu(2)
                            Label { text: cfg.label; color: "#cccccc"; font.pixelSize: units.gu(1.5); width: parent.width * 0.6 }
                            Label { text: root._formatValue(sl.value, cfg.fmt); color: "white"; font.pixelSize: units.gu(1.5); width: parent.width * 0.4; horizontalAlignment: Text.AlignRight }
                        }
                        QQC2.Slider {
                            id: sl; width: parent.width - units.gu(2)
                            from: cfg.from; to: cfg.to; value: parent.displayValue
                            onMoved: { root._s[cfg.key] = value; previewDebounce.restart() }
                        }
                    }
                }

                // ── Color ─────────────────────────────────────────────────────
                Item { width: parent.width; height: units.gu(4)
                    Label { anchors.centerIn: parent; text: "Color"; color: "white"; font.bold: true; font.pixelSize: units.gu(1.8) } }

                Repeater {
                    model: [
                        { key: "saturation", label: "Saturation", from: 0.0, to: 2.0, def: 1.05, fmt: "pct" },
                    ]
                    delegate: Column {
                        width: settingsCol.width
                        leftPadding: units.gu(1); rightPadding: units.gu(1)
                        bottomPadding: units.gu(0.8); spacing: units.gu(0.1)
                        property var cfg: modelData
                        Connections { target: root; onSettingsReset: { sl2.value = cfg.key in root._s ? root._s[cfg.key] : cfg.def } }
                        Row {
                            width: parent.width - units.gu(2)
                            Label { text: cfg.label; color: "#cccccc"; font.pixelSize: units.gu(1.5); width: parent.width * 0.6 }
                            Label { text: root._formatValue(sl2.value, cfg.fmt); color: "white"; font.pixelSize: units.gu(1.5); width: parent.width * 0.4; horizontalAlignment: Text.AlignRight }
                        }
                        QQC2.Slider {
                            id: sl2; width: parent.width - units.gu(2)
                            from: cfg.from; to: cfg.to
                            value: cfg.key in root._s ? root._s[cfg.key] : cfg.def
                            onMoved: { root._s[cfg.key] = value; previewDebounce.restart() }
                        }
                    }
                }

                // ── Presets ───────────────────────────────────────────────────
                Item { width: parent.width; height: units.gu(4)
                    Label { anchors.centerIn: parent; text: "Presets"; color: "white"; font.bold: true; font.pixelSize: units.gu(1.8) } }

                Row {
                    x: units.gu(1)
                    width: parent.width - units.gu(2)
                    height: units.gu(5)
                    spacing: units.gu(1)

                    Repeater {
                        model: ["AUTO", "FLAT"]
                        Rectangle {
                            width: (parent.width - units.gu(1)) / 2
                            height: units.gu(4.5)
                            color: pArea.pressed ? "#44ffffff" : "#22ffffff"
                            border.color: "#44ffffff"; border.width: 1
                            radius: units.gu(0.5)
                            Label { anchors.centerIn: parent; text: modelData; color: "white"; font.pixelSize: units.gu(1.6); font.bold: true }
                            MouseArea { id: pArea; anchors.fill: parent; onClicked: root._applyPreset(modelData.toLowerCase()) }
                        }
                    }
                }

                Item { width: parent.width; height: units.gu(0.5) }

                // ── White Balance ─────────────────────────────────────────────
                Item { width: parent.width; height: units.gu(4)
                    Label { anchors.centerIn: parent; text: "White Balance"; color: "white"; font.bold: true; font.pixelSize: units.gu(1.8) } }

                Repeater {
                    model: [
                        { key: "temperature", label: "Temperature", from: 2000, to: 8000, def: 5000,  fmt: "kelvin" },
                        { key: "tint",        label: "Tint",        from: -50,  to: 50,   def: 0,     fmt: "tint"   },
                    ]
                    delegate: Column {
                        width: settingsCol.width
                        leftPadding: units.gu(1); rightPadding: units.gu(1)
                        bottomPadding: units.gu(0.8); spacing: units.gu(0.1)
                        property var cfg: modelData
                        Connections { target: root; onSettingsReset: { sl3.value = cfg.key in root._s ? root._s[cfg.key] : cfg.def } }
                        Row {
                            width: parent.width - units.gu(2)
                            Label { text: cfg.label; color: "#cccccc"; font.pixelSize: units.gu(1.5); width: parent.width * 0.6 }
                            Label { text: root._formatValue(sl3.value, cfg.fmt); color: "white"; font.pixelSize: units.gu(1.5); width: parent.width * 0.4; horizontalAlignment: Text.AlignRight }
                        }
                        QQC2.Slider {
                            id: sl3; width: parent.width - units.gu(2)
                            from: cfg.from; to: cfg.to
                            value: cfg.key in root._s ? root._s[cfg.key] : cfg.def
                            onMoved: { root._s[cfg.key] = value; previewDebounce.restart() }
                        }
                    }
                }

                // ── Detail ────────────────────────────────────────────────────
                Item { width: parent.width; height: units.gu(4)
                    Label { anchors.centerIn: parent; text: "Detail"; color: "white"; font.bold: true; font.pixelSize: units.gu(1.8) } }

                Repeater {
                    model: [
                        { key: "sharpen0", label: "Sharpness", from: 1.0, to: 4.0, def: 2.0,  fmt: "num1" },
                        { key: "sharpen1", label: "Detail",    from: 1.0, to: 4.0, def: 2.0,  fmt: "num1" },
                        { key: "pop",      label: "Pop",       from: 1.0, to: 2.0, def: 1.25, fmt: "num2" },
                    ]
                    delegate: Column {
                        width: settingsCol.width
                        leftPadding: units.gu(1); rightPadding: units.gu(1)
                        bottomPadding: units.gu(0.8); spacing: units.gu(0.1)
                        property var cfg: modelData
                        Connections { target: root; onSettingsReset: { sl4.value = cfg.key in root._s ? root._s[cfg.key] : cfg.def } }
                        Row {
                            width: parent.width - units.gu(2)
                            Label { text: cfg.label; color: "#cccccc"; font.pixelSize: units.gu(1.5); width: parent.width * 0.6 }
                            Label { text: root._formatValue(sl4.value, cfg.fmt); color: "white"; font.pixelSize: units.gu(1.5); width: parent.width * 0.4; horizontalAlignment: Text.AlignRight }
                        }
                        QQC2.Slider {
                            id: sl4; width: parent.width - units.gu(2)
                            from: cfg.from; to: cfg.to
                            value: cfg.key in root._s ? root._s[cfg.key] : cfg.def
                            onMoved: { root._s[cfg.key] = value; previewDebounce.restart() }
                        }
                    }
                }

                // ── Processing ────────────────────────────────────────────────
                Item { width: parent.width; height: units.gu(4)
                    Label { anchors.centerIn: parent; text: "Processing"; color: "white"; font.bold: true; font.pixelSize: units.gu(1.8) } }

                Column {
                    width: parent.width
                    leftPadding: units.gu(1); rightPadding: units.gu(1)
                    bottomPadding: units.gu(1); spacing: units.gu(0.4)
                    Row {
                        width: parent.width - units.gu(2)
                        Label { text: "Merge frames"; color: "#cccccc"; font.pixelSize: units.gu(1.5); width: parent.width * 0.6 }
                        Label { text: (mergeSlider.value * 4) + " frames"; color: "white"; font.pixelSize: units.gu(1.5); width: parent.width * 0.4; horizontalAlignment: Text.AlignRight }
                    }
                    QQC2.Slider { id: mergeSlider; width: parent.width - units.gu(2); from: 1; to: 4; stepSize: 1; value: 2 }
                    Row {
                        width: parent.width - units.gu(2)
                        spacing: units.gu(1)
                        Label { text: "Save DNG"; color: "#cccccc"; font.pixelSize: units.gu(1.5); anchors.verticalCenter: parent.verticalCenter }
                        QQC2.Switch { id: dngSwitch; checked: false }
                    }
                }
            }
        }
    }

    // ── Floating Save (FAB) ───────────────────────────────────────────────────
    Rectangle {
        anchors { bottom: parent.bottom; right: parent.right; margins: units.gu(2) }
        width: units.gu(7); height: width; radius: width / 2
        color: fabArea.pressed ? "#0097A7" : "#00BCD4"
        z: 20

        Label {
            anchors.centerIn: parent
            text: "↓"
            color: "white"
            font.pixelSize: units.gu(3)
            font.bold: true
        }

        MouseArea {
            id: fabArea
            anchors.fill: parent
            onClicked: {
                if (root.frames.length === 0) return
                var ts = root.frames[root.selectedIndex].timestamp
                var nFrames = mergeSlider.value * 4
                var s = Object.assign({}, root._s)
                s["dng"] = dngSwitch.checked
                camera.saveBurstFrame(ts, nFrames, JSON.stringify(s))
                root.closed()
            }
        }
    }

    // ── Signals / debounce ────────────────────────────────────────────────────
    Connections {
        target: camera
        onBurstPreviewReady:   { root.previewToken++ }
        onBurstThumbnailReady: { root.thumbnailVersion++ }
    }

    Timer {
        id: previewDebounce
        interval: 300
        onTriggered: {
            root.settingsJson = JSON.stringify(root._s)
            root._requestPreview()
        }
    }

    // ── Populate when frames arrive ───────────────────────────────────────────
    function onFramesReady(frameList) {
        root.previewToken     = 0
        root.thumbnailVersion = 0
        root.frames           = frameList
        root.selectedIndex    = 0
        for (var i = 0; i < frameList.length; ++i) {
            if (frameList[i].sharpest) { root.selectedIndex = i; break }
        }
        root._requestPreview()
    }
}
