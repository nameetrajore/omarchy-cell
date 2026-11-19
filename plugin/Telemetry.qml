import QtQuick
import Quickshell.Io
import qs.Commons
import "Model.js" as Model

Item {
    id: root
    visible: false
    property bool active: false
    property var report: ({})
    property string errorKey: ""
    readonly property string error: errorKey ? catalog.t(errorKey) : ""
    readonly property var strings: catalog
    readonly property string backend: decodeURIComponent(String(Qt.resolvedUrl("../build/cell")).replace(/^file:\/\//,""))
    Translations { id: catalog; directory: Qt.resolvedUrl("i18n") }
    function refresh() { if (active && !query.running) query.running=true }
    onActiveChanged: if (active) refresh()
    Process {
        id: query
        command: [root.backend,"report","--json"]
        stdout: StdioCollector {
            onStreamFinished: {
                let next=Model.report(text,Date.now()/1000)
                if(next) { root.report=next; root.errorKey="" }
                else root.errorKey="error.invalid"
            }
        }
        onExited: function(code) { if(code!==0) root.errorKey="error.history" }
    }
    Timer { interval: 30000; repeat: true; running: root.active; onTriggered: root.refresh() }
    Timer { interval: 10000; running: query.running; onTriggered: { query.signal(9); root.errorKey="error.timeout" } }
}
