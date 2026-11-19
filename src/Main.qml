import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1080; height: 820
    minimumWidth: 760; minimumHeight: 600
    visible: true
    title: "Cell"
    color: theme.colors.background
    palette.window: theme.colors.background
    palette.windowText: theme.colors.foreground
    palette.text: theme.colors.foreground
    palette.buttonText: theme.colors.foreground
    palette.base: theme.colors.surface
    palette.button: theme.colors.surface
    Material.foreground: theme.colors.foreground
    Material.background: theme.colors.surface
    Material.theme: theme.colors.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    property var b: backend.battery
    property bool helpVisible: false
    property color muted: theme.colors.muted
    property int graphMode: 0
    function fmt(value, digits) {
        let language = translator.language
        return value === undefined || value < 0 ? "—" : Number(value).toLocaleString(Qt.locale(), 'f', digits)
    }
    function metric(value, unit, digits) { return value === undefined || value < 0 ? "—" : fmt(value, digits) + " " + unit }
    function date(ts) { let language = translator.language; return new Date(ts*1000).toLocaleString(Qt.locale(), "ddd HH:mm") }
    function state(s) {
        switch(s) {
        case "Discharging": return qsTr("On battery")
        case "Charging": return qsTr("Charging")
        case "Full": return qsTr("Fully charged")
        case "Not charging": return qsTr("Not charging")
        default: return qsTr("Unknown state")
        }
    }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = false }
    component Card: Rectangle {
        property string label
        property string value
        property string note
        color: theme.colors.surface; radius: theme.radius
        border.color: theme.colors.border
        implicitHeight: Math.max(116, cardContents.implicitHeight + 36)
        ColumnLayout {
            id: cardContents
            anchors.fill: parent; anchors.margins: 18; spacing: 6
            Label { text: label; color: win.muted; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: value; font.pixelSize: 28; font.weight: Font.DemiBold; color: theme.colors.foreground }
            Label { objectName: "cardNote"; text: note; color: win.muted; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        ScrollView {
            anchors.fill: parent; anchors.margins: 28
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width; spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 4
                        Label { text: "Cell"; font.pixelSize: 30; font.weight: Font.Bold }
                        Label { text: qsTr("Understand your battery."); color: win.muted }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle { width: 8; height: 8; radius: 4; color: theme.accent }
                    Label { text: demoMode ? qsTr("Demo · synthetic data") : backend.error.length > 0 ? qsTr("Recording unavailable") : qsTr("Recording every 30 seconds"); color: theme.accent }
                    ToolButton { text: "?"; Accessible.name: qsTr("Keyboard shortcuts"); onClicked: win.helpVisible = !win.helpVisible }
                }
                Label {
                    visible: backend.error.length > 0
                    text: backend.error; color: theme.colors.error; wrapMode: Text.Wrap; Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: win.state(b.state); font.pixelSize: 17; font.weight: Font.DemiBold }
                    Label { text: b.model || ""; color: win.muted; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: qsTr("Updated every 30 seconds"); color: win.muted; font.pixelSize: 12 }
                }
                GridLayout {
                    Layout.fillWidth: true; columns: win.width < 850 ? 2 : 4; columnSpacing: 12; rowSpacing: 12
                    Card { Layout.fillWidth: true; Layout.preferredWidth: 1; label: qsTr("Charge"); value: win.metric(b.charge, "%", 0); note: qsTr("%1 Wh remaining").arg(win.fmt(b.energy, 1)) }
                    Card { Layout.fillWidth: true; Layout.preferredWidth: 1; label: b.state === "Charging" ? qsTr("Charging power") : qsTr("Battery power"); value: win.metric(b.watts, "W", 1); note: qsTr("Whole-system measurement") }
                    Card { Layout.fillWidth: true; Layout.preferredWidth: 1; label: qsTr("Battery health"); value: win.metric(b.health, "%", 1); note: qsTr("%1 charge cycles").arg(win.fmt(b.cycles, 0)) }
                    Card { Layout.fillWidth: true; Layout.preferredWidth: 1; label: qsTr("Estimated runtime"); value: win.metric(b.hours, "h", 1); note: qsTr("At the current power draw") }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 274; color: theme.colors.surface; radius: theme.radius; border.color: theme.colors.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 18; spacing: 10
                        RowLayout {
                            Label { text: qsTr("Recent history"); font.pixelSize: 16; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                id: selector
                                objectName: "graphSelector"
                                model: [qsTr("Power · W"), qsTr("Charge · %"), qsTr("Full capacity · Wh")]
                                Accessible.name: qsTr("Graph metric")
                                onCurrentIndexChanged: { win.graphMode = currentIndex; chart.requestPaint() }
                                implicitWidth: 215
                            }
                        }
                        Item {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Canvas {
                                id: chart; anchors.fill: parent
                                property var samples: backend.points
                                onSamplesChanged: requestPaint()
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                Connections { target: theme; function onChanged() { chart.requestPaint() } }
                                Connections { target: translator; function onChanged() { chart.requestPaint() } }
                                onPaint: {
                                    let ctx = getContext('2d'); ctx.reset()
                                    let key = ["watts", "charge", "full"][win.graphMode]
                                    let ps = samples
                                    let hi = win.graphMode === 1 ? 100 : 1
                                    for (let s of ps) if (s[key] >= 0) hi = Math.max(hi, s[key] * 1.15)
                                    let left = 44, right = width-8, top = 10, bottom = height-24
                                    ctx.font = '11px sans-serif'; ctx.fillStyle = theme.colors.muted
                                    for(let j=0;j<4;j++) {
                                        let y = top+(bottom-top)*j/3
                                        ctx.strokeStyle=theme.colors.border; ctx.beginPath(); ctx.moveTo(left,y); ctx.lineTo(right,y); ctx.stroke()
                                        ctx.fillText(win.fmt(hi*(1-j/3), 0), 0, y+4)
                                    }
                                    if(ps.length < 2) return
                                    let start=ps[0].ts, span=Math.max(30,ps[ps.length-1].ts-start), prev=null
                                    ctx.strokeStyle=theme.accent; ctx.lineWidth=2; ctx.beginPath()
                                    for(let s of ps) {
                                        if(s[key]<0) { prev=null; continue }
                                        let x=left+(s.ts-start)/span*(right-left), y=bottom-s[key]/hi*(bottom-top)
                                        if(!prev || s.ts-prev.ts>90) ctx.moveTo(x,y); else ctx.lineTo(x,y)
                                        prev=s
                                    }
                                    ctx.stroke(); ctx.fillStyle=theme.colors.muted
                                    ctx.fillText(win.date(start), left, height-3)
                                    let end=win.date(ps[ps.length-1].ts)
                                    ctx.fillText(end, Math.max(left,right-ctx.measureText(end).width),height-3)
                                }
                            }
                            Label { anchors.centerIn: parent; visible: backend.points.length < 2; text: qsTr("Collecting the first samples…"); color: win.muted }
                        }
                        Label { text: qsTr("Last 7 days recorded · gaps are not interpolated"); color: win.muted; font.pixelSize: 12 }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true; columns: win.width < 850 ? 1 : 3; columnSpacing: 24; rowSpacing: 8
                    Label { text: qsTr("Temperature: %1").arg(win.metric(b.temperature, "°C", 1)); color: win.muted }
                    Label { text: qsTr("Full: %1 / design: %2").arg(win.metric(b.full, "Wh", 1)).arg(win.metric(b.design, "Wh", 1)); color: win.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Label { text: win.metric(b.voltage, "V", 2); color: win.muted }
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: win.muted
                    text: (b.averageWatts || -1) >= 0
                        ? qsTr("Recorded discharge average: %1 W across %2 h sampled (last 7 days, including this session).").arg(win.fmt(b.averageWatts, 1)).arg(win.fmt(b.sampledHours, 1))
                        : qsTr("A personal power baseline appears after 10 minutes of recorded discharge.")
                }
                Label { visible: (b.devices || 0) > 1; text: qsTr("Multiple batteries found. Showing %1 only.").arg(b.device || ""); color: win.muted }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Discharge sessions"); font.pixelSize: 18; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Label { text: qsTr("Most recent first · last 7 days"); color: win.muted; font.pixelSize: 12 }
                }
                Label {
                    visible: backend.sessions.length === 0
                    text: qsTr("Sessions appear after two consecutive discharge samples. Charging or a recording gap ends a session.")
                    color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true
                }
                Repeater {
                    model: backend.sessions.slice(0, 12)
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: sessionContents.implicitHeight + 32; color: theme.colors.surface; radius: theme.radius; border.color: theme.colors.border
                        GridLayout {
                            id: sessionContents
                            anchors.fill: parent; anchors.margins: 16; columns: win.width < 850 ? 2 : 5; columnSpacing: 20; rowSpacing: 12
                            Label { text: win.date(modelData.start); Layout.fillWidth: true; Layout.columnSpan: win.width < 850 ? 2 : 1 }
                            Label { text: win.metric(modelData.hours*60, "min", 0); Layout.fillWidth: true }
                            Label { text: win.metric(modelData.energy, "Wh", 2); Layout.fillWidth: true }
                            Label { text: qsTr("%1 W avg").arg(win.fmt(modelData.watts, 1)); color: theme.accent; Layout.fillWidth: true }
                            Label { text: qsTr("%1 pp lost").arg(win.fmt(modelData.drop, 1)); color: win.muted; Layout.fillWidth: true }
                        }
                    }
                }
                Label {
                    text: qsTr("Stored only on this computer. Health and runtime are estimates. Session energy integrates measured battery power; it is not per-app attribution.")
                    color: win.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.bottomMargin: 10
                }
            }
        }
        Rectangle {
            anchors.fill: parent; color: Qt.rgba(win.color.r, win.color.g, win.color.b, 0.94); visible: win.helpVisible
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
            Rectangle {
                anchors.centerIn: parent; width: Math.min(460, parent.width - 40); height: 240; color: theme.colors.surface; radius: theme.radius; border.color: theme.colors.border
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 28; spacing: 18
                    Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 22 }
                    Label { text: qsTr("Space — refresh now") }
                    Label { text: qsTr("? — toggle this help") }
                    Label { text: qsTr("Esc — close help") }
                    Label { text: qsTr("Q — quit") }
                }
            }
        }
    }
}
