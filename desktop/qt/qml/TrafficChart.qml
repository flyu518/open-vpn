import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: chart
    objectName: "trafficChart"
    property var provider
    property string sessionId: ""
    property int hours: 1
    property var history: ({ points: [], now: Date.now(), error: "" })
    readonly property var samples: history.points || []
    readonly property real endTime: history.now || Date.now()
    readonly property real startTime: endTime - hours * 3600000
    readonly property real maximum: {
        let value = 1024
        for (const p of samples) value = Math.max(value, p.download, p.upload)
        return value * 1.1
    }
    spacing: 8
    function rate(value) {
        if (value >= 1048576) return (value / 1048576).toFixed(1) + " MB/s"
        if (value >= 1024) return (value / 1024).toFixed(1) + " KB/s"
        return Math.round(value) + " B/s"
    }
    function time(value) { return Qt.formatDateTime(new Date(value), "HH:mm") }
    function refresh() {
        if (provider && sessionId) history = provider.trafficForSession(sessionId, hours)
    }
    onHoursChanged: refresh()
    onSessionIdChanged: refresh()
    onSamplesChanged: plot.requestPaint()
    onMaximumChanged: plot.requestPaint()
    RowLayout {
        Layout.fillWidth: true
        Label { text: "流量走势"; color: "#202b38"; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        Repeater {
            model: [1, 24]
            delegate: Button {
                required property int modelData
                text: modelData + " 小时"
                checked: chart.hours === modelData
                implicitWidth: 68
                implicitHeight: 30
                onClicked: chart.hours = modelData
                contentItem: Label { text: parent.text; color: parent.checked ? "#275b99" : "#647285"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 6; color: parent.checked ? "#eaf1fa" : parent.hovered ? "#f4f6f8" : "#ffffff"; border.color: parent.checked ? "#aec6e5" : "#dce2e9" }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: "● 下载"; color: "#3568a8"; font.pixelSize: 12 }
        Label { text: "● 上传"; color: "#25815f"; font.pixelSize: 12 }
        Item { Layout.fillWidth: true }
        Label { text: "每分钟平均速率"; color: "#647285"; font.pixelSize: 12 }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 152
        color: "#f8fafc"
        radius: 7
        border.color: "#e5eaf0"
        Canvas {
            id: plot
            anchors.fill: parent
            anchors.margins: 12
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                const left = 68, top = 7, right = width - 5, bottom = height - 22
                if (right <= left || bottom <= top) return
                const x = t => left + (Math.max(chart.startTime, Math.min(chart.endTime, t)) - chart.startTime) / (chart.endTime - chart.startTime) * (right - left)
                const y = n => bottom - n / chart.maximum * (bottom - top)
                ctx.font = "11px sans-serif"
                ctx.fillStyle = "#647285"
                ctx.strokeStyle = "#e1e7ee"
                ctx.lineWidth = 1
                for (let i = 0; i <= 2; i++) {
                    const value = chart.maximum * i / 2
                    ctx.beginPath(); ctx.moveTo(left, y(value)); ctx.lineTo(right, y(value)); ctx.stroke()
                    ctx.fillText(chart.rate(value), 0, y(value) + 4)
                }
                ctx.fillText(chart.time(chart.startTime), left, height - 2)
                const label = chart.time(chart.endTime)
                ctx.fillText(label, right - ctx.measureText(label).width, height - 2)
                for (const series of [{ key: "download", color: "#3568a8" }, { key: "upload", color: "#25815f" }]) {
                    ctx.strokeStyle = series.color; ctx.fillStyle = series.color; ctx.lineWidth = 2
                    ctx.beginPath()
                    let previous = -1
                    for (const point of chart.samples) {
                        const center = (point.from + point.to) / 2
                        if (point.from > previous) ctx.moveTo(x(center), y(point[series.key]))
                        else ctx.lineTo(x(center), y(point[series.key]))
                        previous = point.to
                    }
                    ctx.stroke()
                    if (chart.samples.length) {
                        const point = chart.samples[chart.samples.length - 1]
                        ctx.beginPath(); ctx.arc(x((point.from + point.to) / 2), y(point[series.key]), 2.5, 0, 2 * Math.PI); ctx.fill()
                    }
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: chart.samples.length === 0
            text: "暂无历史数据，连接后开始记录"
            color: "#647285"
            font.pixelSize: 12
            padding: 8
            background: Rectangle { color: "#f8fafc"; radius: 4 }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onWheel: function(wheel) { wheel.accepted = false }
            property var hoveredSample: null
            onPositionChanged: function(mouse) {
                const time = chart.startTime + Math.max(0, Math.min(1, (mouse.x - 80) / Math.max(1, width - 97))) * (chart.endTime - chart.startTime)
                hoveredSample = null
                for (const p of chart.samples) if (p.from <= time && p.to >= time) { hoveredSample = p; break }
            }
            ToolTip.visible: containsMouse && hoveredSample !== null
            ToolTip.text: hoveredSample ? chart.time(hoveredSample.from) + "–" + chart.time(hoveredSample.to) + "\n下载 " + chart.rate(hoveredSample.download) + " · 上传 " + chart.rate(hoveredSample.upload) : ""
        }
    }
    Label {
        Layout.fillWidth: true
        text: chart.history.error || "本地保留最近 24 小时；界面退出或断线期间未采样，曲线留空。"
        color: chart.history.error ? "#b83f49" : "#647285"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
}
