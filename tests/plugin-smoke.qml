import QtQuick
import QtQuick.Window
import QtTest as Testing
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import "Cell" as Cell
import "Cell/Model.js" as Model

ShellRoot {
    id: root
    FileView { id: fixture; path: decodeURIComponent(String(Qt.resolvedUrl("fixture.json")).replace(/^file:\/\//,"")); blockLoading: true }
    Translations { id: catalog; directory: Qt.resolvedUrl("Cell/i18n") }
    property var data: Model.report(fixture.text(),Date.now()/1000)
    property int step: 0
    property real pinnedTimestamp: 0
    function check(ok, message) { if(!ok) { console.error("TEST FAILURE: "+message); Qt.exit(1); throw Error(message) } }
    function find(item,name) {
        if(item.objectName === name) return item
        for(let child of item.children || []) { let match=find(child,name); if(match) return match }
        return null
    }
    function click(name) {
        let item=find(details,name)
        check(!!item && item.visible && item.width>0 && item.height>0,"button visible: "+name)
        let p=item.mapToItem(details,0,0)
        check(p.y>=0 && p.y+item.height<=details.height+1,"button fits: "+name)
        check(events.mouseClick(item,item.width/2,item.height/2,Qt.LeftButton,Qt.NoModifier,0),"click: "+name)
    }
    function save(name) { capture.stop(); settle.name=name; settle.start() }
    Timer {
        id: settle
        property string name
        interval: 200
        onTriggered: window.contentItem.grabToImage(function(result) {
            let path=decodeURIComponent(String(Qt.resolvedUrl(settle.name+".png")).replace(/^file:\/\//,""))
            check(result.saveToFile(path),"save "+settle.name)
            root.step++; capture.start()
        })
    }
    Window {
        id: window
        width: 450; height: 457; visible: true; color: Commons.Color.popups.background
        Testing.TestEvent { id: events }
        Rectangle { anchors.fill: parent; color: Commons.Color.popups.background }
        Cell.BatteryDetails {
            id: details
            anchors.fill: parent; anchors.margins: 16
            battery: root.data ? root.data.battery : ({})
            report: root.data || ({})
            strings: catalog
        }
    }
    Timer {
        id: capture
        interval: 300; running: true; repeat: true
        onTriggered: {
            check(!!root.data,"valid fixture")
            switch(root.step) {
            case 0: save("overview"); break
            case 1:
                click("metric1"); check(details.metric==="charge","charge toggle")
                click("range15"); check(details.minutes===15,"range toggle")
                let graph=find(details,"historyGraph")
                check(graph.points.length<root.data.points.length,"time range filters samples")
                events.mouseMove(graph,graph.width/2,graph.height/2,0,Qt.NoButton,Qt.NoModifier)
                check(graph.selectedIndex>=0,"hover selects measurement")
                events.mouseClick(graph,graph.width/2,graph.height/2,Qt.LeftButton,Qt.NoModifier,0)
                check(graph.pinned,"click pins measurement")
                root.pinnedTimestamp=graph.selected.ts
                root.data=Object.assign({},root.data,{points:root.data.points.slice()})
                save("charge"); break
            case 2:
                let pinned=find(details,"historyGraph")
                check(pinned.pinned && pinned.selected.ts===root.pinnedTimestamp,"pinned sample survives refresh")
                click("tab1"); check(details.tab===1,"compare tab"); check(find(details,"tab1").selected && !find(details,"tab0").selected,"selected tab indicator"); save("compare"); break
            case 3:
                click("tab2"); check(details.tab===2,"history tab")
                click("historyEnergy"); check(details.energyMode,"energy toggle")
                click("session1"); check(details.sessionIndex===1,"session selection")
                save("history"); break
            case 4:
                window.width=370
                click("tab0"); details.changeTab(-1); check(details.tab===2,"keyboard tab wrap")
                details.tab=0
                save("compact"); break
            case 5:
                root.data={battery:root.data.battery,context:{},analysis:{status:"warming",matches:0},points:[],sessions:[]}
                details.tab=1
                save("empty"); break
            case 6:
                root.data=Model.report(fixture.text(),Date.now()/1000)
                window.width=450; window.height=362; details.embedded=true; details.tab=0
                save("embedded-overview"); break
            case 7: click("tab1"); save("embedded-compare"); break
            case 8:
                click("tab2"); click("session4"); check(details.sessionIndex===4,"last embedded session fits")
                let row=find(details,"session4"), value=find(row,"sessionValue")
                check(value.text.endsWith(" Wh") && value.width>=value.implicitWidth,"session value and unit fit")
                check(value.mapToItem(row,0,0).x+value.width<row.width,"session value has right padding")
                save("embedded-history"); break
            default:
                console.log("Tab clicks, metric/range toggles, graph hover/pin/refresh, session selection, compact and embedded layouts passed")
                Qt.exit(0)
            }
        }
    }
}
