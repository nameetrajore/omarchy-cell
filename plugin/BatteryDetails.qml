import QtQuick
import QtQuick.Layouts
import qs.Commons
import qs.Commons as Commons
import qs.Ui

Item {
    id: root
    required property var battery
    required property var report
    required property var strings
    property string error: ""
    property bool embedded: false
    property int tab: 0
    property string metric: "watts"
    property int minutes: 60
    property int sessionIndex: 0
    property bool energyMode: false
    readonly property var analysis: report.analysis || ({})
    readonly property var context: report.context || ({})
    readonly property var sessions: report.sessions || []
    readonly property var session: sessions.length ? sessions[Math.min(sessionIndex,sessions.length-1)] : null
    readonly property bool ready: analysis.status === "ready"
    implicitHeight: Style.space(embedded ? 330 : 425)
    clip: true
    function n(value, decimals) { return typeof value === "number" && value >= 0 ? I18n.formatNumber(value,decimals) : "—" }
    function date(ts) { return new Date(ts*1000).toLocaleString(Qt.locale(I18n.formatLocale),"ddd HH:mm") }
    function changeTab(delta) { tab=(tab+delta+3)%3 }
    function profile(value) {
        if(value === "power-saver") return strings.t("profile.saver")
        if(value === "balanced") return strings.t("profile.balanced")
        if(value === "performance") return strings.t("profile.performance")
        return value || "—"
    }
    component Caption: PlainText {
        font.pixelSize: Style.font.caption
        opacity: .65
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }
    component Meter: Item {
        property real value: -1
        property real maximum: 100
        property color fill: Commons.Color.accent
        implicitHeight: Style.space(6)
        Rectangle { anchors.fill: parent; color: Commons.Color.popups.text; opacity: .10; radius: Math.min(2,Style.cornerRadius) }
        Rectangle { height: parent.height; width: parent.width*Math.max(0,Math.min(1,value/Math.max(.1,maximum))); color: fill; radius: Math.min(2,Style.cornerRadius) }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: Style.space(10)
        RowLayout {
            visible: !root.embedded
            Layout.fillWidth: true
            PlainText { text: "Cell"; font.pixelSize: Style.font.title; font.bold: true; Layout.fillWidth: true }
            Caption { text: root.profile(root.context.profile); Layout.maximumWidth: root.width*.45 }
            Rectangle {
                width: Style.space(6); height: width; radius: width/2
                color: root.error || root.report.stale ? Commons.Color.urgent : Commons.Color.accent
                opacity: root.context.ts ? 1:.3
            }
            Button {
                text: "?"; tooltipText: root.strings.t("help")
                fontSize: Style.font.caption; horizontalPadding: Style.space(6); verticalPadding: Style.space(2)
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: Style.space(4)
            Repeater {
                model: [root.strings.t("tab.overview"),root.strings.t("tab.compare"),root.strings.t("tab.history")]
                Button {
                    required property int index
                    required property string modelData
                    objectName: "tab"+index
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    text: modelData; selected: root.tab===index; fontSize: Style.font.bodySmall
                    onClicked: root.tab=index
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData
                    Accessible.selected: root.tab===index
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.leftMargin: Style.space(8); anchors.rightMargin: Style.space(8)
                        height: Math.max(1,Style.space(2)); color: Commons.Color.accent
                        visible: root.tab===index
                    }
                }
            }
            Button {
                visible: root.embedded
                text: "?"; tooltipText: root.strings.t("help")
                fontSize: Style.font.caption; horizontalPadding: Style.space(5)
            }
        }
        PlainText {
            visible: root.error.length>0
            Layout.fillWidth: true; Layout.maximumHeight: Style.space(30)
            text: root.error; color: Commons.Color.urgent; maximumLineCount: 2; elide: Text.ElideRight
            font.pixelSize: Style.font.caption
        }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: root.tab
            ColumnLayout {
                spacing: Style.space(10)
                RowLayout {
                    visible: !root.embedded
                    Layout.fillWidth: true
                    Repeater {
                        model: [ {label:root.strings.t("charge"),value:root.n(root.battery.charge,0)+"%"},
                            {label:root.strings.t("draw"),value:root.n(root.battery.watts,1)+" W"},
                            {label:root.strings.t("runtime"),value:root.n(root.battery.hours,1)+" h"} ]
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; Layout.preferredWidth: 1
                            spacing: Style.space(3)
                            Caption { text: modelData.label; Layout.fillWidth: true }
                            PlainText { text: modelData.value; font.pixelSize: Style.font.title; font.bold: true; Layout.fillWidth: true }
                        }
                    }
                }
                Meter { visible: !root.embedded; Layout.fillWidth: true; value: root.battery.charge === undefined ? -1:root.battery.charge }
                RowLayout {
                    Layout.fillWidth: true; spacing: Style.space(4)
                    Repeater {
                        model: [root.strings.t("power"),root.strings.t("charge")]
                        Button {
                            required property int index
                            required property string modelData
                            objectName: "metric"+index
                            text: modelData; selected: root.metric === (index===0 ? "watts":"charge")
                            fontSize: Style.font.caption; verticalPadding: Style.space(3)
                            onClicked: root.metric=index===0 ? "watts":"charge"
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Repeater {
                        model: [15,60]
                        Button {
                            required property int modelData
                            objectName: "range"+modelData
                            text: I18n.formatNumber(modelData)+"m"; selected: root.minutes===modelData
                            fontSize: Style.font.caption; verticalPadding: Style.space(3); horizontalPadding: Style.space(6)
                            onClicked: root.minutes=modelData
                        }
                    }
                }
                HistoryGraph {
                    id: graph
                    objectName: "historyGraph"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    samples: root.report.points || []
                    metric: root.metric; unit: root.metric === "watts" ? "W":"%"; minutes: root.minutes
                    Caption { anchors.centerIn: parent; visible: graph.points.length<2; text: root.strings.t("collecting") }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Caption { text: root.strings.t("health").arg(root.n(root.battery.health,1)); Layout.fillWidth: true }
                    Caption { visible: !root.embedded; text: root.strings.t("cycles").arg(root.n(root.battery.cycles,0)) }
                }
            }
            ColumnLayout {
                spacing: Style.space(10)
                PlainText {
                    text: root.ready ? (root.analysis.deltaWatts>0 ? "+":"")+I18n.formatNumber(root.analysis.deltaWatts,1)+" W" : root.strings.t(root.report.stale ? "offline":"learning")
                    font.pixelSize: Style.font.display; font.bold: true; color: Commons.Color.accent
                }
                Caption {
                    Layout.fillWidth: true
                    text: root.ready ? root.strings.t("against").arg(I18n.formatNumber(root.analysis.matches)) : root.strings.t("need.sessions")
                }
                ColumnLayout {
                    visible: root.ready
                    Layout.fillWidth: true
                    Repeater {
                        model: [root.strings.t("latest"),root.strings.t("typical")]
                        ColumnLayout {
                            required property int index
                            required property string modelData
                            property real value: !root.ready ? 0 : index===0 ? root.analysis.current.watts : root.analysis.baselineWatts
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                Caption { text: modelData; Layout.fillWidth: true }
                                PlainText { text: root.n(value,1)+" W" }
                            }
                            Meter { Layout.fillWidth: true; implicitHeight: Style.space(14); value: parent.value; maximum: root.ready ? Math.max(root.analysis.current.watts,root.analysis.baselineWatts)*1.1 : 1; fill: parent.index===0 ? Commons.Color.accent:Commons.Color.popups.text }
                        }
                    }
                }
                RowLayout {
                    visible: !root.ready; Layout.fillWidth: true; spacing: Style.space(6)
                    Repeater {
                        model: 3
                        Meter { required property int index; Layout.fillWidth: true; implicitHeight: Style.space(12); value: (root.analysis.matches || 0)>index ? 100:0 }
                    }
                    Caption { text: root.n(Math.min(3,root.analysis.matches || 0),0)+" / 3" }
                }
                RowLayout {
                    visible: root.context.cpu>=0 || root.context.brightness>=0
                    Layout.fillWidth: true
                    Caption { visible: root.context.cpu>=0; text: root.strings.t("cpu")+" "+root.n(root.context.cpu,0)+"%"; Layout.fillWidth: true }
                    Caption { visible: root.context.brightness>=0; text: root.strings.t("brightness")+" "+root.n(root.context.brightness,0)+"%" }
                }
                Caption { visible: (root.context.apps || []).length>0; text: root.strings.t("cpu.apps"); Layout.fillWidth: true }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Style.space(6)
                    Repeater {
                        model: (root.context.apps || []).slice(0,3)
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Caption { text: modelData.name; Layout.preferredWidth: root.width*.38; opacity: 1 }
                            Meter { Layout.fillWidth: true; value: modelData.cpu }
                            Caption { text: root.n(modelData.cpu,1)+"%"; Layout.preferredWidth: Style.space(44); horizontalAlignment: Text.AlignRight }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                spacing: Style.space(8)
                RowLayout {
                    Layout.fillWidth: true
                    Caption { text: root.strings.t("sessions"); Layout.fillWidth: true }
                    Button { objectName: "historyPower"; text: "W"; selected: !root.energyMode; fontSize: Style.font.caption; verticalPadding: Style.space(3); onClicked: root.energyMode=false }
                    Button { objectName: "historyEnergy"; text: "Wh"; selected: root.energyMode; fontSize: Style.font.caption; verticalPadding: Style.space(3); onClicked: root.energyMode=true }
                }
                Repeater {
                    model: root.sessions
                    Item {
                        id: sessionRow
                        required property int index
                        required property var modelData
                        objectName: "session"+index
                        property real value: root.energyMode ? modelData.energy:modelData.watts
                        property real maximum: {
                            let max=1
                            for(let s of root.sessions) max=Math.max(max,root.energyMode ? s.energy:s.watts)
                            return max*1.1
                        }
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.maximumHeight: Style.space(42)
                        Rectangle {
                            anchors.fill: parent; radius: Style.cornerRadius
                            color: Commons.Color.accent
                            opacity: root.sessionIndex===index ? .12 : rowMouse.containsMouse ? .06:0
                        }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: Style.space(10); anchors.rightMargin: Style.space(10)
                            spacing: Style.space(10)
                            Caption {
                                text: root.date(modelData.start)
                                Layout.preferredWidth: Math.min(Style.space(108),root.width*.3); Layout.minimumWidth: 0
                                opacity: root.sessionIndex===index ? 1:.65
                            }
                            Meter { Layout.fillWidth: true; implicitHeight: Style.space(8); value: sessionRow.value; maximum: sessionRow.maximum }
                            PlainText {
                                objectName: "sessionValue"
                                text: root.n(sessionRow.value,1)+(root.energyMode ? " Wh":" W")
                                Layout.preferredWidth: Style.space(72); Layout.minimumWidth: implicitWidth
                                horizontalAlignment: Text.AlignRight; font.pixelSize: Style.font.caption
                            }
                        }
                        MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.sessionIndex=index }
                        Accessible.role: Accessible.Button
                        Accessible.name: root.date(modelData.start)+", "+root.n(value,1)+(root.energyMode ? " Wh":" W")
                        Accessible.onPressAction: root.sessionIndex=index
                    }
                }
                Caption { visible: root.sessions.length===0; text: root.strings.t("sessions.empty"); Layout.fillWidth: true }
                Item { Layout.fillHeight: true }
                PlainText {
                    visible: !!root.session
                    Layout.fillWidth: true; font.bold: true
                    text: root.session ? root.strings.t("session.detail").arg(root.n(root.session.hours*60,0)).arg(root.n(root.session.watts,1)).arg(root.n(root.session.energy,1)) : ""
                }
            }
        }
    }
}
