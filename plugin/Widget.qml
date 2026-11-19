import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui
import "Model.js" as Model

Panel {
    id: root
    moduleName: "local.cell"
    ipcTarget: "local.cell"
    property var battery: ({})
    property var report: ({})
    property string error: ""
    readonly property string backend: decodeURIComponent(String(Qt.resolvedUrl("cell-backend")).replace(/^file:\/\//, ""))
    readonly property bool vertical: bar ? bar.vertical : false
    readonly property string icon: battery.state === "Charging" ? "󰂄" : "󰁹"
    Translations { id: catalog; directory: Qt.resolvedUrl("i18n") }
    function number(value, decimals) { return typeof value === "number" && value >= 0 ? I18n.formatNumber(value, decimals) : "—" }
    function refreshStatus() { if (!statusProc.running) statusProc.running = true }
    function refreshReport() { if (!reportProc.running) reportProc.running = true }
    onOpenedChanged: if (opened) { refreshStatus(); refreshReport() }
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight

    Process {
        id: statusProc
        command: [root.backend, "status", "--json"]
        stdout: StdioCollector {
            onStreamFinished: {
                let next = Model.status(text)
                if (next) root.battery = next
                else root.error = catalog.t("error.invalid")
            }
        }
        onExited: function(code) { if (code !== 0) root.error = catalog.t("error.backend") }
    }
    Process {
        id: reportProc
        command: [root.backend, "report", "--json"]
        stdout: StdioCollector {
            onStreamFinished: {
                let next = Model.report(text, Date.now()/1000)
                if (next) { root.report = next; root.battery = next.battery; root.error = "" }
                else root.error = catalog.t("error.invalid")
            }
        }
        onExited: function(code) { if (code !== 0) root.error = catalog.t("error.history") }
    }
    Timer { interval: 30000; running: true; repeat: true; triggeredOnStart: true; onTriggered: root.refreshStatus() }
    Timer { interval: 30000; running: root.opened; repeat: true; onTriggered: root.refreshReport() }
    Timer { interval: 10000; running: statusProc.running; onTriggered: { statusProc.signal(9); root.error = catalog.t("error.timeout") } }
    Timer { interval: 10000; running: reportProc.running; onTriggered: { reportProc.signal(9); root.error = catalog.t("error.timeout") } }

    BarIconButton {
        id: button
        anchors.fill: parent
        bar: root.bar
        text: root.icon
        labelText: root.vertical ? "" : root.number(root.battery.watts, 1) + " W"
        slotSize: Style.bar.iconSlot * (root.vertical ? 1 : 2)
        // Only plugin-owned catalog text reaches the shell's tooltip renderer.
        tooltipText: catalog.t("tooltip")
        onPressed: root.toggle()
    }
    KeyboardPanel {
        id: panel
        anchorItem: button
        owner: root
        bar: root.bar
        open: root.opened
        focusTarget: panel.loadedContent
        contentWidth: fittedContentWidth(Style.space(450))
        contentHeight: fittedContentHeight(loadedContentHeight, Style.space(470))
        content: PanelKeyCatcher {
            id: keys
            anchors.fill: parent
            implicitHeight: details.implicitHeight
            onCloseRequested: root.close()
            onTabRequested: function(direction) { root.switchPanel(direction) }
            onActivateRequested: root.refreshReport()
            onTextKey: function(text) {
                if (text.toLowerCase() === "r") root.refreshReport()
                else if (["1","2","3"].indexOf(text)>=0) details.tab=Number(text)-1
            }
            onMoveRequested: function(dx, dy) { if (dx) details.changeTab(dx) }
            BatteryDetails {
                id: details
                anchors.fill: parent
                battery: root.battery
                report: root.report
                error: root.error
                strings: catalog
            }
        }
    }
}
