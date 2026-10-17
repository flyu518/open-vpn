// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 960
    height: 640
    minimumWidth: 820
    minimumHeight: 560
    visible: true
    title: "OpenVPN"
    color: "#f4f6f8"
    font.pixelSize: 13
    palette.window: "#ffffff"
    palette.windowText: "#202b38"
    palette.text: "#202b38"
    palette.base: "#ffffff"
    palette.buttonText: "#202b38"
    palette.highlight: "#3568a8"
    palette.highlightedText: "#ffffff"

    readonly property color ink: "#202b38"
    readonly property color muted: "#647285"
    readonly property color line: "#dce2e9"
    readonly property color accent: "#3568a8"
    readonly property color green: "#25815f"
    readonly property color red: "#b83f49"
    property string detailSessionId: ""
    property var detail: ({})
    property int sessionRevision: 0
    readonly property var detailConflicts: { const revision = sessionRevision; return controller.routeConflictInfo(detailSessionId) }
    property string detailDiagnostic: ""
    property bool diagnosticExpanded: false
    property int activeConnectionsAtQuit: 0
    property string credentialSessionId: ""
    property string credentialReason: ""
    property bool credentialPending: false
    property bool forgetPending: false
    property string forgetError: ""
    property string noticeTitle: ""
    property string noticeMessage: ""
    property string routeModeDraft: "compatible"
    property string routeRulePatternDraft: ""
    property string routeRuleTypeDraft: "ip"

    function formatBytes(value) {
        if (value === undefined || value < 0) return "—"
        if (value < 1024) return value + " B"
        if (value < 1048576) return (value / 1024).toFixed(1) + " KB"
        if (value < 1073741824) return (value / 1048576).toFixed(1) + " MB"
        return (value / 1073741824).toFixed(2) + " GB"
    }
    function formatDuration(value) {
        const seconds = Math.max(0, value || 0)
        const hours = Math.floor(seconds / 3600)
        return (hours > 0 ? hours + ":" : "") +
               String(Math.floor(seconds % 3600 / 60)).padStart(2, "0") + ":" +
               String(seconds % 60).padStart(2, "0")
    }
    function readableMessage(message, fallback) {
        const text = String(message || "").trim()
        if (/AUTH_FAILED|authentication failed|invalid (username|password)|认证失败/i.test(text))
            return "用户名或密码不正确，请重新输入。"
        if (/timed? ?out|timeout/i.test(text))
            return "连接超时，请检查网络后重试。"
        if (/connection refused|host not found|network.*unreachable/i.test(text))
            return "无法连接服务器，请检查网络和服务器地址。"
        if (text.length > 180 || /SetupClient/i.test(text) || text.startsWith("{") || text.startsWith("["))
            return fallback
        if (text && !/[\u3400-\u9fff]/.test(text)) return fallback
        return text || fallback
    }
    function refreshDetails() {
        detail = controller.detailsForSession(detailSessionId)
        if (credentialPending) {
            const state = controller.detailsForSession(credentialSessionId).state
            if (state === "connected") {
                credentialPending = false
                credentialsDialog.close()
            } else if (state === "failed") {
                credentialPending = false
                credentialReason = "连接未成功，请检查凭据后重试。"
            }
        }
    }
    function openDetails(id, diagnostic) {
        detailSessionId = id
        detailDiagnostic = diagnostic || ""
        diagnosticExpanded = false
        refreshDetails()
        detailsDialog.open()
    }
    function scrollDiagnosticToEnd() {
        if (!diagnosticExpanded || !detailFlick) return
        detailFlick.contentY = Math.max(0, detailFlick.contentHeight - detailFlick.height)
    }
    function notify(title, message) {
        noticeTitle = title
        noticeMessage = readableMessage(message, "操作未成功，请在连接详情中查看诊断信息。")
        noticeTimer.restart()
    }
    onDiagnosticExpandedChanged: if (diagnosticExpanded) diagnosticScrollTimer.restart()
    onClosing: function(close) {
        if (controller.quitting) return
        if (!controller.trayAvailable) {
            close.accepted = false
            controller.quitApplication()
        } else {
            close.accepted = false
            visible = false
        }
    }

    // One vector vocabulary for the application, controls and all dialog levels.
    component Glyph: Item {
        id: glyph
        property string symbol: "shield"
        property color stroke: window.accent
        implicitWidth: 20
        implicitHeight: 20
        Shape {
            width: 24
            height: 24
            anchors.centerIn: parent
            scale: Math.min(glyph.width, glyph.height) / 24
            ShapePath {
                strokeWidth: 1.8
                strokeColor: glyph.stroke
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: ({
                        "shield": "M12 3L20 6V11C20 16 16.5 19.5 12 22C7.5 19.5 4 16 4 11V6Z M9 12L11 14L15.5 9.5",
                        "plus": "M12 5V19 M5 12H19",
                        "connect": "M5 12H19 M13 6L19 12L13 18",
                        "stop": "M6 6H18V18H6Z",
                        "close": "M6 6L18 18 M18 6L6 18",
                        "retry": "M19 8A8 8 0 1 0 20 14 M19 3V8H14",
                        "grip": "M9 5H9.1 M15 5H15.1 M9 12H9.1 M15 12H15.1 M9 19H9.1 M15 19H15.1",
                        "settings": "M4 7H20 M4 17H20 M9 4V10 M15 14V20",
                        "info": "M12 10V17 M12 6H12.1 M12 2A10 10 0 1 0 12 22A10 10 0 1 0 12 2",
                        "lock": "M7 10V7A5 5 0 0 1 17 7V10 M5 10H19V21H5Z M12 14V17"
                    })[glyph.symbol] || ""
                }
            }
        }
    }
    component AppButton: Button {
        id: control
        property string symbol: ""
        property string tone: "normal"
        readonly property color foreground: !enabled ? "#8b96a4" :
                                            tone === "primary" ? "#ffffff" :
                                            tone === "danger" ? window.red : window.ink
        implicitWidth: Math.max(80, group.implicitWidth + 28)
        implicitHeight: 36
        padding: 0
        hoverEnabled: true
        font.pixelSize: 13
        DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
        contentItem: Item {
            implicitWidth: group.implicitWidth
            implicitHeight: group.implicitHeight
            Row {
                id: group
                anchors.centerIn: parent
                spacing: 7
                Glyph {
                    objectName: "buttonIcon"
                    visible: control.symbol !== ""
                    width: 16
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: control.symbol
                    stroke: control.foreground
                }
                Label {
                    text: control.text
                    color: control.foreground
                    font.pixelSize: 13
                    font.weight: control.tone === "primary" ? Font.DemiBold : Font.Normal
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        background: Rectangle {
            radius: 6
            color: !control.enabled ? "#f0f2f5" :
                   control.tone === "primary" ? (control.down ? "#28548b" : control.hovered ? "#4076b8" : window.accent) :
                   control.down ? "#e8edf3" : control.hovered ? "#f0f4f8" : "#ffffff"
            border.color: control.activeFocus ? window.accent :
                          control.tone === "primary" ? color :
                          control.tone === "danger" ? "#e4bec2" : window.line
            border.width: control.activeFocus ? 2 : 1
        }
    }
    component AppField: TextField {
        implicitHeight: 38
        font.pixelSize: 14
        selectByMouse: true
        leftPadding: 11
        rightPadding: 11
        color: window.ink
        placeholderTextColor: "#84909e"
        background: Rectangle {
            radius: 6
            color: parent.enabled ? "#ffffff" : "#f4f6f8"
            border.color: parent.activeFocus ? window.accent : window.line
            border.width: parent.activeFocus ? 2 : 1
        }
    }
    component SessionItem: Item {
        property string sessionKey: ""
        property Item handle
        property string diagnostic: ""
    }
    component DialogActions: DialogButtonBox {
        padding: 16
        spacing: 8
        alignment: Qt.AlignRight
        background: Rectangle {
            color: "transparent"
            Rectangle { width: parent.width; height: 1; color: window.line }
        }
    }
    component AppDialog: Dialog {
        id: shell
        parent: Overlay.overlay
        anchors.centerIn: parent
        popupType: Popup.Item
        modal: true
        focus: true
        margins: 24
        width: Math.min(460, window.width - 48)
        height: Math.min(implicitHeight, window.height - 48)
        padding: 20
        topPadding: 16
        bottomPadding: 20
        closePolicy: Popup.CloseOnEscape
        font.pixelSize: 13
        background: Rectangle { color: "#ffffff"; radius: 10; border.color: window.line }
        header: Item {
            implicitHeight: 56
            Label {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: shell.title
                font.pixelSize: 17
                font.weight: Font.DemiBold
                color: window.ink
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.line }
        }
        Overlay.modal: Rectangle { color: "#330f1d2d" }
    }

    Connections {
        target: controller.sessionModel
        function onDataChanged() { window.sessionRevision++; window.refreshDetails() }
        function onModelReset() { window.sessionRevision++; window.refreshDetails() }
        function onRowsRemoved() { window.sessionRevision++; window.refreshDetails() }
        function onRowsInserted() { window.sessionRevision++; window.refreshDetails() }
    }
    Connections {
        target: controller
        function onCredentialsRequired(sessionId, username, reason) {
            if (window.credentialSessionId !== sessionId || !credentialsDialog.visible)
                credentialUsername.text = username
            window.credentialSessionId = sessionId
            window.credentialReason = reason
            window.credentialPending = false
            credentialPassword.clear()
            credentialsDialog.open()
        }
        function onNotice(title, message) {
            if (window.forgetPending && (title === "已删除" || title === "删除失败")) {
                window.forgetPending = false
                if (title === "已删除") {
                    forgetDialog.close()
                    window.notify(title, message)
                } else {
                    window.forgetError = message
                }
                return
            }
            window.notify(title, message)
        }
        function onSettingsRequested() { settingsDialog.open() }
        function onAboutRequested() { aboutDialog.open() }
        function onQuitConfirmationRequired(activeConnections) {
            window.activeConnectionsAtQuit = activeConnections
            window.show()
            window.raise()
            window.requestActivate()
            quitDialog.open()
        }
    }

    header: Rectangle {
        implicitHeight: 76
        color: "#ffffff"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.line }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 9
                color: "#eaf0f8"
                Image { anchors.fill: parent; source: "qrc:/openvpn/app-icon.png"; fillMode: Image.PreserveAspectFit; smooth: true }
            }
            ColumnLayout {
                spacing: 2
                Label { text: "OpenVPN"; font.pixelSize: 19; font.weight: Font.DemiBold; color: window.ink }
                Label { text: "多连接管理"; color: window.muted; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }
            AppButton { text: "导入配置"; symbol: "plus"; tone: "primary"; onClicked: controller.importProfile() }
            AppButton { text: "设置"; symbol: "settings"; onClicked: settingsDialog.open() }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        Rectangle {
            visible: controller.serviceError !== "" || controller.storageError !== ""
            Layout.fillWidth: true
            implicitHeight: errorLabel.implicitHeight + 24
            color: "#fff3f2"
            radius: 6
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                Label {
                    id: errorLabel
                    Layout.fillWidth: true
                    text: controller.serviceError || controller.storageError
                    color: window.red
                    wrapMode: Text.Wrap
                }
                AppButton { visible: controller.serviceError !== ""; text: "重新检查"; onClicked: controller.refreshService() }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "连接"; color: window.ink; font.pixelSize: 15; font.weight: Font.DemiBold }
            Label { text: controller.sessionModel.count; color: window.muted }
            Item { Layout.fillWidth: true }
            Label {
                Layout.fillWidth: true
                text: "拖动排序。重叠网段由先连接成功的配置使用，调整后需按顺序重连"
                color: window.muted
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
            }
        }
        ListView {
            id: sessionList
            objectName: "sessionList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: controller.sessionModel
            boundsBehavior: Flickable.StopAtBounds
            interactive: !reorderArea.pressed
            ScrollBar.vertical: ScrollBar {}
            property string draggedId: ""
            property int draggedIndex: -1
            property real pointerY: 0
            property real grabOffset: 0
            property bool reordering: false
            property bool animateReorder: false

            // The grab belongs to the stationary viewport, never to a moving or
            // recycled delegate. ListView alone owns every card's x/y.
            function reorderAtPointer() {
                if (!reordering) return
                const y = Math.max(0, Math.min(height - 1, pointerY)) + contentY
                const index = indexAt(width / 2 + contentX, y)
                const target = itemAtIndex(index) as SessionItem
                if (!target || index === draggedIndex) return
                if (index > draggedIndex && y < target.y + target.height / 2) return
                if (index < draggedIndex && y > target.y + target.height / 2) return
                const offset = contentY - originY
                if (!controller.moveSession(draggedId, target.sessionKey)) {
                    endReorder()
                    return
                }
                draggedIndex = index
                forceLayout()
                contentY = originY + offset
                forceLayout()
            }
            function endReorder() {
                draggedId = ""
                draggedIndex = -1
                reordering = false
                settleTimer.restart()
            }
            Timer { id: settleTimer; interval: 220; onTriggered: sessionList.animateReorder = false }

            delegate: SessionItem {
                id: sessionDelegate
                objectName: "sessionDelegate"
                width: sessionList.width
                height: 92
                sessionKey: model.sessionId
                handle: dragHandle
                diagnostic: [model.error || "", (model.warnings || []).join("\n")].filter(Boolean).join("\n")
                property bool lifted: sessionList.reordering && sessionList.draggedId === sessionKey
                readonly property var conflictInfo: { const revision = window.sessionRevision; return controller.routeConflictInfo(sessionKey) }
                property real targetY: lifted ? sessionList.pointerY - sessionList.grabOffset + sessionList.contentY : y
                property real visualY: 0
                Component.onCompleted: visualY = y
                z: lifted ? 2 : 0
                onTargetYChanged: {
                    positionAnimation.stop()
                    if (!sessionList.animateReorder || lifted) visualY = targetY
                    else { positionAnimation.from = visualY; positionAnimation.to = targetY; positionAnimation.start() }
                }
                NumberAnimation { id: positionAnimation; target: sessionDelegate; property: "visualY"; duration: 180; easing.type: Easing.OutCubic }
                onDiagnosticChanged: {
                    if (window.detailSessionId === sessionKey) window.detailDiagnostic = diagnostic
                }
                Rectangle {
                    objectName: "cardSurface"
                    anchors.fill: parent
                    transform: Translate { y: sessionDelegate.visualY - sessionDelegate.y }
                    radius: 8
                    color: sessionDelegate.lifted ? "#f4f8ff" : "#ffffff"
                    border.color: sessionList.draggedId === sessionDelegate.sessionKey ? window.accent : window.line
                    border.width: 1
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.openDetails(sessionDelegate.sessionKey, sessionDelegate.diagnostic)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 16
                        spacing: 12
                        Item {
                            id: dragHandle
                            objectName: "dragButton"
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 40
                            Glyph { anchors.centerIn: parent; symbol: "grip"; stroke: "#8a97a7"; width: 18; height: 24 }
                        }
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: 8
                            color: model.connected ? "#edf6f1" : "#edf2f8"
                            Glyph {
                                anchors.centerIn: parent
                                symbol: "shield"
                                stroke: model.connected ? window.green : window.accent
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                Layout.fillWidth: true
                                text: model.name || "未命名配置"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: window.ink
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (model.remoteHost || model.profilePath || "未读取到服务器地址") +
                                      (model.connected && sessionDelegate.conflictInfo.summary ?
                                       " · " + window.formatDuration(model.durationSeconds) + "  ↓ " + window.formatBytes(model.downloadBytes) + "  ↑ " + window.formatBytes(model.uploadBytes) : "")
                                color: window.muted
                                font.pixelSize: 13
                                elide: Text.ElideMiddle
                            }
                            Label {
                                objectName: "routeConflictHint"
                                Layout.fillWidth: true
                                text: sessionDelegate.conflictInfo.summary || (model.connected ? "运行 " + window.formatDuration(model.durationSeconds) +
                                      "    ↓ " + window.formatBytes(model.downloadBytes) + "    ↑ " + window.formatBytes(model.uploadBytes) +
                                      (model.warnings.length > 0 ? " · 部分路由重叠" : "") :
                                      model.failed ? window.readableMessage(model.error, "连接未成功，请打开详情查看诊断信息。") :
                                      model.warnings.length > 0 ? window.readableMessage(model.warnings[0], "连接存在路由提示，请打开详情查看诊断信息。") : "")
                                textFormat: Text.PlainText
                                visible: text !== ""
                                color: model.failed ? window.red :
                                       sessionDelegate.conflictInfo.hasWarning ? "#996515" : window.muted
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                ToolTip.visible: conflictHover.containsMouse && sessionDelegate.conflictInfo.hasWarning
                                ToolTip.text: text
                                MouseArea {
                                    id: conflictHover
                                    anchors.fill: parent
                                    enabled: sessionDelegate.conflictInfo.hasWarning
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: window.openDetails(sessionDelegate.sessionKey, sessionDelegate.diagnostic)
                                }
                            }
                        }
                        AppButton {
                            objectName: "sessionActionButton"
                            Layout.preferredWidth: 116
                            text: model.state === "connected" ? "断开" :
                                  model.state === "connecting" ? "取消连接" :
                                  model.state === "disconnecting" ? "正在断开" :
                                  model.failed ? "重试" : "连接"
                            symbol: model.state === "connected" ? "stop" :
                                    model.state === "connecting" ? "close" :
                                    model.state === "disconnecting" ? "" :
                                    model.failed ? "retry" : "connect"
                            tone: model.connected ? "danger" : model.connecting ? "normal" : "primary"
                            enabled: model.state !== "disconnecting"
                            onClicked: controller.toggleConnection(sessionDelegate.sessionKey)
                        }
                    }
                }
            }
            MouseArea {
                id: reorderArea
                anchors.fill: parent
                z: 10
                preventStealing: true
                property real pressY: 0
                onPressed: function(mouse) {
                    const index = sessionList.indexAt(mouse.x + sessionList.contentX, mouse.y + sessionList.contentY)
                    const card = sessionList.itemAtIndex(index) as SessionItem
                    if (!card) { mouse.accepted = false; return }
                    const point = mapToItem(card.handle, mouse.x, mouse.y)
                    if (point.x < 0 || point.x > card.handle.width || point.y < 0 || point.y > card.handle.height) {
                        mouse.accepted = false
                        return
                    }
                    sessionList.cancelFlick()
                    settleTimer.stop()
                    sessionList.animateReorder = true
                    sessionList.draggedId = card.sessionKey
                    sessionList.draggedIndex = index
                    sessionList.pointerY = mouse.y
                    sessionList.grabOffset = mouse.y - (card.y - sessionList.contentY)
                    pressY = mouse.y
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || sessionList.draggedId === "") return
                    sessionList.pointerY = mouse.y
                    if (Math.abs(mouse.y - pressY) > 8) sessionList.reordering = true
                    sessionList.reorderAtPointer()
                }
                onReleased: sessionList.endReorder()
                onCanceled: sessionList.endReorder()
            }
            Timer {
                interval: 24
                repeat: true
                running: reorderArea.pressed && sessionList.reordering
                onTriggered: {
                    const step = sessionList.pointerY < 28 ? -12 : sessionList.pointerY > sessionList.height - 28 ? 12 : 0
                    if (!step) return
                    sessionList.contentY = Math.max(sessionList.originY,
                        Math.min(sessionList.originY + Math.max(0, sessionList.contentHeight - sessionList.height),
                                 sessionList.contentY + step))
                    sessionList.forceLayout()
                    sessionList.reorderAtPointer()
                }
            }
            }
            Label {
                anchors.centerIn: parent
                visible: controller.sessionModel.count === 0
                text: "尚未导入配置\n点击“导入配置”选择 .ovpn 文件"
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.6
                font.pixelSize: 14
                color: window.muted
            }
        }

    // A reserved status area cannot cover a connection or a dialog action.
    footer: Rectangle {
        id: noticeBar
        objectName: "noticeBar"
        visible: window.noticeMessage !== ""
        implicitHeight: visible ? 64 : 0
        color: "#eef3fa"
        Rectangle { width: parent.width; height: 1; color: window.line }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 12
            Label { text: window.noticeTitle; font.weight: Font.DemiBold; color: window.ink }
            Label {
                Layout.fillWidth: true
                text: window.noticeMessage
                color: window.muted
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            AppButton { text: "关闭提示"; onClicked: window.noticeMessage = "" }
        }
    }
    Timer { id: noticeTimer; interval: 7000; onTriggered: window.noticeMessage = "" }
    Timer { id: diagnosticScrollTimer; interval: 0; onTriggered: window.scrollDiagnosticToEnd() }

    AppDialog {
        id: detailsDialog
        objectName: "detailsDialog"
        title: "连接详情"
        width: Math.min(620, window.width - 48)
        onOpened: { detailFlick.contentY = 0; trafficChart.refresh() }
        contentItem: ScrollView {
            id: detailScroll
            implicitHeight: Math.min(detailBody.implicitHeight, window.height - 216)
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Connections {
                target: detailScroll.contentItem
                function onContentHeightChanged() {
                    if (window.diagnosticExpanded) diagnosticScrollTimer.restart()
                }
            }
            contentItem: Flickable {
                id: detailFlick
                clip: true
                contentWidth: width
                contentHeight: detailBody.implicitHeight + 4
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                id: detailBody
                width: detailFlick.width
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    text: window.detail.name || "未命名配置"
                    color: window.ink
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                ColumnLayout {
                    objectName: "routeConflictDetails"
                    Layout.fillWidth: true
                    spacing: 8
                    visible: window.detailConflicts.hasWarning
                    Label { text: "路由冲突 · 建立连接时"; color: "#996515"; font.weight: Font.DemiBold }
                    Label {
                        Layout.fillWidth: true
                        visible: window.detailConflicts.groups.length === 0
                        text: "服务未提供可识别的冲突对象和网段，请重连后再查看。"
                        color: window.muted
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: window.detailConflicts.groups
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: conflictBody.implicitHeight + 20
                            color: "#fffaf1"
                            border.color: "#ebd7b2"
                            radius: 6
                            ColumnLayout {
                                id: conflictBody
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8
                                Label {
                                    Layout.fillWidth: true
                                    text: "与「" + modelData.name + "」重叠 · " + modelData.status
                                    textFormat: Text.PlainText
                                    font.weight: Font.DemiBold
                                    color: window.ink
                                    wrapMode: Text.Wrap
                                }
                                Repeater {
                                    model: modelData.routes
                                    delegate: Label {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        text: "本连接请求 " + modelData.candidateRoute + " · " + modelData.result +
                                              "\n建立时由对方处理的路由：" + modelData.activeRoute
                                        textFormat: Text.PlainText
                                        color: window.muted
                                        wrapMode: Text.Wrap
                                        lineHeight: 1.2
                                    }
                                }
                }
            }
                    }
            Label {
                        Layout.fillWidth: true
                        text: "如需通过本连接访问这些地址，请先断开冲突连接，再重连本连接。对方断开后，本连接不会自动补装此前跳过的路由。"
                        color: window.muted
                        wrapMode: Text.Wrap
                    }
                }
                TrafficChart {
                    id: trafficChart
                    Layout.fillWidth: true
                    provider: controller
                    sessionId: window.detailSessionId
                    Connections {
                        target: controller
                        function onTrafficHistoryChanged() { if (detailsDialog.visible) trafficChart.refresh() }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 9
                    Label { text: "服务器"; color: window.muted }
                    Label { Layout.fillWidth: true; text: window.detail.remoteHost || "未读取"; elide: Text.ElideMiddle }
                    Label { text: "用户名"; color: window.muted }
                    Label { Layout.fillWidth: true; text: window.detail.username || "未保存"; elide: Text.ElideRight }
                    Label { text: "连接记录"; color: window.muted }
                    Label { text: (window.detail.connectionCount || 0) + " 次 · " + (window.detail.configLines || 0) + " 行配置" }
                    Label { text: "导入来源"; color: window.muted }
                    Label { Layout.fillWidth: true; text: window.detail.profilePath || "不可用"; elide: Text.ElideMiddle }
                }
                Label {
                    Layout.fillWidth: true
                    text: "导入成功后，配置已保存到应用；删除原文件不影响连接。"
                    color: window.muted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.line }
                Label { text: "路由信息"; color: window.ink; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: "当前策略：" + (window.detail.conflictMode === "strict" ? "严格模式" : "兼容模式"); color: window.muted }
                }
                Label {
                    Layout.fillWidth: true
                    text: "兼容模式会跳过与活动 VPN 重叠的路由；严格模式检测到冲突时拒绝连接。重叠路由由先连接成功的配置使用。需要让另一条连接接管时，请断开当前占用路由的连接，再断开并重新连接本连接。"
                    color: window.muted
                    wrapMode: Text.Wrap
                    lineHeight: 1.3
                }
                Repeater {
                    model: window.detail.routeStrategy ? (window.detail.routeStrategy.rules || []) : []
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.pattern
                        color: window.muted
                        wrapMode: Text.WrapAnywhere
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: "IP/CIDR 规则会参与地址路由处理；域名后缀会加入 macOS 分流 DNS 范围，需要当前 VPN 提供 DNS 服务器。模糊匹配和正则规则不支持。"
                    color: window.muted
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: ["connected", "connecting", "disconnecting"].indexOf(window.detail.state) >= 0
                    text: "删除配置或让新路由策略生效前，请先返回列表断开连接。"
                    color: window.muted
                    wrapMode: Text.Wrap
                }
                AppButton {
                    visible: window.detailDiagnostic !== ""
                    text: window.diagnosticExpanded ? "收起诊断信息" : "查看诊断信息"
                    onClicked: window.diagnosticExpanded = !window.diagnosticExpanded
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    visible: window.diagnosticExpanded && window.detailDiagnostic !== ""
                    clip: true
                    TextArea {
                        objectName: "diagnosticText"
                        text: window.detailDiagnostic
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        font.pixelSize: 13
                        color: window.muted
                        background: Rectangle { color: "#f4f6f8"; radius: 6; border.color: window.line }
                    }
                }
                }
            }
        }
        footer: DialogActions {
            AppButton {
                objectName: "routePolicyButton"
                text: "编辑路由策略"
                onClicked: {
                    window.routeModeDraft = window.detail.conflictMode || "compatible"
                    routePolicyDialog.errorText = ""
                    routePolicyDialog.open()
                }
            }
            AppButton {
                text: "编辑名称"
                onClicked: {
                    renameField.text = window.detail.name || ""
                    renameDialog.errorText = ""
                    renameDialog.open()
                }
            }
            AppButton { text: "导出配置"; onClicked: controller.exportProfile(window.detailSessionId) }
            AppButton {
                text: "删除凭据"
                onClicked: { window.forgetError = ""; forgetDialog.open() }
            }
            AppButton {
                text: "删除配置"
                tone: "danger"
                enabled: ["connected", "connecting", "disconnecting"].indexOf(window.detail.state) < 0
                onClicked: { deleteProfileDialog.errorText = ""; deleteProfileDialog.open() }
            }
            AppButton { objectName: "detailsCloseButton"; text: "关闭"; onClicked: detailsDialog.close() }
        }
    }
    AppDialog {
        id: routePolicyDialog
        objectName: "routePolicyDialog"
        title: "编辑路由策略"
        property string errorText: ""
        contentItem: ScrollView {
            id: routePolicyScroll
            implicitHeight: Math.min(routePolicyBody.implicitHeight,
                                     Math.max(180, window.height - 220))
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentItem: Flickable {
                id: routePolicyFlick
                clip: true
                contentWidth: width
                contentHeight: routePolicyBody.implicitHeight + 4
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: routePolicyBody
                    width: routePolicyFlick.width
                    spacing: 4
            Label {
                Layout.fillWidth: true
                text: "选择发生路由重叠时的处理方式。策略保存后，已连接的 VPN 不会立即改变。"
                color: window.muted
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    Layout.fillWidth: true
                    text: "兼容模式"
                    tone: window.routeModeDraft === "compatible" ? "primary" : "normal"
                    onClicked: window.routeModeDraft = "compatible"
                }
                AppButton {
                    Layout.fillWidth: true
                    text: "严格模式"
                    tone: window.routeModeDraft === "strict" ? "primary" : "normal"
                    onClicked: window.routeModeDraft = "strict"
                }
            }
            Label {
                Layout.fillWidth: true
                text: window.routeModeDraft === "strict" ?
                      "发生冲突时拒绝建立新连接。" :
                      "尽量建立连接，并跳过与活动 VPN 重叠的路由。"
                color: window.ink
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: "如果当前连接已经建立，请先断开它，再重新连接，新的策略和路由裁决才会生效。"
                color: "#996515"
                wrapMode: Text.Wrap
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.line }
            Label { text: "路由规则"; color: window.ink; font.weight: Font.DemiBold }
            Label {
                Layout.fillWidth: true
                text: window.routeRuleTypeDraft === "domainSuffix" ?
                      "域名后缀会加入当前 VPN 的 macOS 分流 DNS 范围；当前 VPN 需要提供 DNS 服务器，IP 路由仍由 VPN 配置提供。" :
                      "IP/CIDR 使用地址范围匹配。优先规则要求冲突时先断开对方；排除规则不会在当前 VPN 安装匹配路由。"
                color: window.muted
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    Layout.fillWidth: true
                    text: "IP/CIDR"
                    tone: window.routeRuleTypeDraft === "ip" ? "primary" : "normal"
                    onClicked: {
                        window.routeRuleTypeDraft = "ip"
                        window.routeRulePatternDraft = ""
                    }
                }
                AppButton {
                    Layout.fillWidth: true
                    text: "域名后缀"
                    tone: window.routeRuleTypeDraft === "domainSuffix" ? "primary" : "normal"
                    onClicked: {
                        window.routeRuleTypeDraft = "domainSuffix"
                        window.routeRulePatternDraft = ""
                    }
                }
            }
            AppField {
                id: routeRuleField
                Layout.fillWidth: true
                placeholderText: window.routeRuleTypeDraft === "domainSuffix" ?
                                 "域名后缀，例如 *.corp.example.com" :
                                 "IP 或 CIDR，例如 10.20.0.0/16"
                text: window.routeRulePatternDraft
                onTextChanged: window.routeRulePatternDraft = text
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    Layout.fillWidth: true
                    text: "优先当前连接"
                    onClicked: {
                        const type = window.routeRuleTypeDraft === "domainSuffix" ?
                                     "domainSuffix" : (routeRuleField.text.indexOf("/") >= 0 ? "cidr" : "ip")
                        if (controller.addRouteRule(window.detailSessionId, type, routeRuleField.text, "prefer")) {
                            routeRuleField.clear(); window.refreshDetails()
                        }
                    }
                }
                AppButton {
                    Layout.fillWidth: true
                    text: "排除当前连接"
                    onClicked: {
                        const type = window.routeRuleTypeDraft === "domainSuffix" ?
                                     "domainSuffix" : (routeRuleField.text.indexOf("/") >= 0 ? "cidr" : "ip")
                        if (controller.addRouteRule(window.detailSessionId, type, routeRuleField.text, "exclude")) {
                            routeRuleField.clear(); window.refreshDetails()
                        }
                    }
                }
            }
            Repeater {
                model: window.detail.routeStrategy ? (window.detail.routeStrategy.rules || []) : []
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: ruleRow.implicitHeight + 12
                    color: "#f4f6f8"
                    radius: 5
                        RowLayout {
                            id: ruleRow
                            anchors.fill: parent
                            anchors.margins: 6
                            Label {
                                Layout.fillWidth: true
                                text: (modelData.action === "exclude" ? "排除 · " : "优先 · ") +
                                      (modelData.type === "domainSuffix" ? "域名后缀 · " : "") + modelData.pattern
                                color: window.ink
                                elide: Text.ElideMiddle
                            }
                            AppButton {
                                text: "删除"
                                implicitWidth: 64
                                onClicked: { controller.removeRouteRule(window.detailSessionId, index); window.refreshDetails() }
                            }
                        }
                    }
                }
            Label {
                Layout.fillWidth: true
                visible: routePolicyDialog.errorText !== ""
                text: routePolicyDialog.errorText
                color: window.red
                wrapMode: Text.Wrap
            }
        }
            }
        }
        footer: DialogActions {
            AppButton { text: "取消"; onClicked: routePolicyDialog.reject() }
            AppButton {
                text: "保存"
                tone: "primary"
                onClicked: {
                    if (controller.setConflictMode(window.detailSessionId, window.routeModeDraft)) {
                        window.refreshDetails()
                        routePolicyDialog.close()
                    } else {
                        routePolicyDialog.errorText = controller.storageError || "路由策略保存失败，请重试。"
                    }
                }
            }
        }
    }
    AppDialog {
        id: renameDialog
        objectName: "renameDialog"
        title: "编辑连接名称"
        property string errorText: ""
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: "连接名称"; color: window.muted }
            AppField { id: renameField; Layout.fillWidth: true; placeholderText: "连接名称"; maximumLength: 120 }
            Label { Layout.fillWidth: true; visible: renameDialog.errorText !== ""; text: renameDialog.errorText; color: window.red; wrapMode: Text.Wrap }
        }
        onOpened: { renameField.forceActiveFocus(); renameField.selectAll() }
        footer: DialogActions {
            AppButton { text: "取消"; onClicked: renameDialog.reject() }
            AppButton {
                text: "保存"
                tone: "primary"
                enabled: renameField.text.trim().length > 0
                onClicked: {
                    const saved = controller.renameSession(window.detailSessionId, renameField.text)
                    window.refreshDetails()
                    if (saved) renameDialog.close()
                    else renameDialog.errorText = controller.storageError || "保存未成功，请重试。"
                }
            }
        }
    }
    AppDialog {
        id: deleteProfileDialog
        objectName: "deleteProfileDialog"
        title: "删除配置"
        property string errorText: ""
        contentItem: ColumnLayout {
            spacing: 12
            Label { Layout.fillWidth: true; text: "确定删除“" + (window.detail.name || "") + "”？"; wrapMode: Text.Wrap; font.pixelSize: 14 }
            Label { Layout.fillWidth: true; text: "仅从连接列表移除，原始 .ovpn 文件会保留。"; color: window.muted; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; visible: deleteProfileDialog.errorText !== ""; text: deleteProfileDialog.errorText; color: window.red; wrapMode: Text.Wrap }
        }
        footer: DialogActions {
            AppButton { text: "取消"; onClicked: deleteProfileDialog.reject() }
            AppButton {
                text: "删除"
                tone: "danger"
                enabled: ["connected", "connecting", "disconnecting"].indexOf(window.detail.state) < 0
                onClicked: {
                    const removed = controller.removeProfile(window.detailSessionId)
                    window.refreshDetails()
                    if (removed) {
                        deleteProfileDialog.close()
                        detailsDialog.close()
                    } else deleteProfileDialog.errorText = controller.storageError || "删除未成功，请先断开连接。"
                }
            }
        }
    }
    AppDialog {
        id: forgetDialog
        objectName: "forgetDialog"
        title: "删除已保存凭据"
        closePolicy: window.forgetPending ? Popup.NoAutoClose : Popup.CloseOnEscape
        contentItem: ColumnLayout {
            spacing: 12
            Label { Layout.fillWidth: true; text: "删除此配置保存在系统安全存储中的用户名和密码？"; wrapMode: Text.Wrap; font.pixelSize: 14 }
            Label { Layout.fillWidth: true; text: "下次连接时需要重新输入凭据。当前连接不受影响。"; color: window.muted; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; visible: window.forgetError !== ""; text: window.forgetError; color: window.red; wrapMode: Text.Wrap }
        }
        footer: DialogActions {
            AppButton { text: "取消"; enabled: !window.forgetPending; onClicked: forgetDialog.reject() }
            AppButton {
                text: window.forgetPending ? "正在删除…" : "删除"
                tone: "danger"
                enabled: !window.forgetPending
                onClicked: { window.forgetPending = true; window.forgetError = ""; controller.forgetCredentials(window.detailSessionId) }
            }
        }
    }
    AppDialog {
        id: credentialsDialog
        objectName: "credentialsDialog"
        title: "输入 VPN 凭据"
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: window.credentialPending ? "正在验证凭据并连接…" :
                      window.readableMessage(window.credentialReason, "请输入用户名和密码后连接。")
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                color: window.muted
            }
            AppField { id: credentialUsername; Layout.fillWidth: true; placeholderText: "用户名"; enabled: !window.credentialPending }
            AppField { id: credentialPassword; Layout.fillWidth: true; placeholderText: "密码"; echoMode: TextInput.Password; enabled: !window.credentialPending }
            CheckBox { id: rememberCredentials; text: "连接成功后保存到系统安全存储"; checked: true; enabled: !window.credentialPending; font.pixelSize: 13 }
        }
        onOpened: credentialUsername.forceActiveFocus()
        onClosed: credentialPassword.clear()
        onRejected: {
            if (window.credentialPending) controller.toggleConnection(window.credentialSessionId)
            window.credentialPending = false
            controller.cancelCredentials(window.credentialSessionId)
        }
        footer: DialogActions {
            AppButton { text: "取消"; onClicked: credentialsDialog.reject() }
            AppButton {
                text: window.credentialPending ? "正在连接…" : "连接"
                tone: "primary"
                enabled: !window.credentialPending && credentialUsername.text.trim().length > 0 && credentialPassword.text.length > 0
                onClicked: {
                    window.credentialPending = true
                    controller.submitCredentials(window.credentialSessionId, credentialUsername.text,
                                                 credentialPassword.text, rememberCredentials.checked)
                }
            }
        }
    }
    AppDialog {
        id: settingsDialog
        objectName: "settingsDialog"
        title: "桌面设置"
        width: Math.min(520, window.width - 48)
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label { text: "登录后自动启动"; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Label { Layout.fillWidth: true; text: "启动后保留在状态栏或系统托盘中。"; color: window.muted; wrapMode: Text.Wrap }
                }
                Switch {
                    checked: controller.startupEnabled
                    enabled: !controller.startupBusy
                    onToggled: controller.setStartupEnabled(checked)
                }
            }
            Label { Layout.fillWidth: true; visible: controller.startupError !== ""; text: controller.startupError; color: window.red; wrapMode: Text.Wrap }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.line }
            Label { text: "配置索引"; color: window.muted }
            Label { Layout.fillWidth: true; text: controller.storageLocation; wrapMode: Text.WrapAnywhere; color: window.muted }
        }
        footer: DialogActions {
            AppButton { text: "退出界面"; onClicked: controller.quitApplication() }
            AppButton { objectName: "settingsCloseButton"; text: "关闭"; onClicked: settingsDialog.close() }
        }
    }
    AppDialog {
        id: aboutDialog
        objectName: "aboutDialog"
        title: "关于 OpenVPN"
        width: Math.min(520, window.width - 48)
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                spacing: 12
                Glyph { Layout.preferredWidth: 36; Layout.preferredHeight: 36 }
                ColumnLayout {
                    spacing: 4
                    Label { text: "OpenVPN"; font.pixelSize: 20; font.weight: Font.DemiBold }
                    Label { text: "版本 " + controller.appVersion + " · 多连接桌面客户端"; color: window.muted }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: controller.updateMessage !== ""
                text: controller.updateMessage
                color: window.muted
                wrapMode: Text.Wrap
                maximumLineCount: 6
                elide: Text.ElideRight
            }
            RowLayout {
                spacing: 8
                AppButton {
                    text: controller.updateState === "checking" ? "检查中…" : "检查更新"
                    enabled: controller.updateState !== "checking" && controller.updateState !== "downloading"
                    onClicked: controller.checkForUpdates()
                }
                AppButton {
                    visible: controller.updateAvailable
                    text: controller.updateInstallerAvailable ? "下载更新" : "打开发布页"
                    tone: "primary"
                    onClicked: controller.updateInstallerAvailable ? updateDialog.open() : controller.openUpdatePage()
                }
            }
            Label { Layout.fillWidth: true; text: "仅在此处手动检查更新。下载安装包前会再次确认。"; color: window.muted; wrapMode: Text.Wrap }
        }
        footer: DialogActions { AppButton { text: "关闭"; onClicked: aboutDialog.close() } }
    }
    AppDialog {
        id: quitDialog
        objectName: "quitDialog"
        title: "退出 OpenVPN"
        contentItem: ColumnLayout {
            spacing: 12
            Label { Layout.fillWidth: true; text: "确定退出 OpenVPN 界面？"; font.pixelSize: 14; wrapMode: Text.Wrap }
            Label {
                Layout.fillWidth: true
                text: window.activeConnectionsAtQuit > 0 ?
                      "当前有 " + window.activeConnectionsAtQuit + " 条活动连接。退出界面不会断开 VPN，连接将继续在后台运行。要断开连接，请取消并返回列表操作。" :
                      "退出后将关闭窗口与托盘图标。"
                color: window.muted
                wrapMode: Text.Wrap
            }
        }
        onRejected: controller.cancelQuitApplication()
        onClosed: { if (controller.quitConfirmationPending) controller.cancelQuitApplication() }
        footer: DialogActions {
            AppButton { text: "取消"; onClicked: quitDialog.reject() }
            AppButton { text: "退出界面"; tone: "primary"; onClicked: { quitDialog.close(); controller.quitApplication(false) } }
        }
    }
    AppDialog {
        id: updateDialog
        objectName: "updateDialog"
        title: "下载更新"
        contentItem: ColumnLayout {
            spacing: 12
            Label { Layout.fillWidth: true; text: "下载并打开 " + controller.updateInstallerName + "？"; font.pixelSize: 14; wrapMode: Text.WrapAnywhere }
            Label { Layout.fillWidth: true; text: "下载完成后将打开系统安装程序，由你确认安装。"; color: window.muted; wrapMode: Text.Wrap }
        }
        footer: DialogActions {
            AppButton { text: "取消"; onClicked: updateDialog.reject() }
            AppButton { text: "下载并打开"; tone: "primary"; onClicked: { controller.downloadUpdate(); updateDialog.close() } }
        }
    }
}
