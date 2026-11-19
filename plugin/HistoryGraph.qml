import QtQuick
import qs.Commons
import qs.Commons as Commons

Item {
    id: root
    property var samples: []
    property string metric: "watts"
    property string unit: "W"
    property int minutes: 60
    property int selectedIndex: -1
    property bool pinned: false
    property real pinnedTimestamp: 0
    readonly property var points: {
        if (!samples.length) return []
        let cutoff = samples[samples.length-1].ts - minutes*60
        return samples.filter(p => p.ts >= cutoff)
    }
    readonly property var selected: selectedIndex >= 0 && selectedIndex < points.length ? points[selectedIndex] : null
    readonly property real ceiling: {
        if (metric === "charge") return 100
        let max = 1
        for (let p of points) if (p[metric] >= 0) max = Math.max(max, p[metric])
        return Math.ceil(max*1.15)
    }
    readonly property int plotLeft: Style.space(30)
    readonly property int plotBottom: Math.max(10, height-Style.space(22))
    readonly property int plotTop: Style.space(22)
    readonly property real span: points.length > 1 ? Math.max(30,points[points.length-1].ts-points[0].ts) : 30
    function positionX(ts) { return plotLeft+(width-plotLeft-4)*(ts-points[0].ts)/span }
    function positionY(value) { return plotBottom-(plotBottom-plotTop)*value/ceiling }
    function time(ts) { return new Date(ts*1000).toLocaleTimeString(Qt.locale(I18n.formatLocale),"HH:mm") }
    function selectAt(position) {
        if (!points.length) return
        let nearest=0, distance=Infinity
        for (let i=0;i<points.length;i++) {
            let d=Math.abs(positionX(points[i].ts)-position)
            if(d<distance) { nearest=i; distance=d }
        }
        selectedIndex=nearest
    }
    function resetSelection() { selectedIndex=-1; pinned=false; pinnedTimestamp=0 }
    onPointsChanged: {
        if(pinned) {
            let index=points.findIndex(p => p.ts===pinnedTimestamp)
            if(index>=0) selectedIndex=index
            else resetSelection()
        } else selectedIndex=-1
        plot.requestPaint()
    }
    onMetricChanged: { resetSelection(); plot.requestPaint() }
    onCeilingChanged: plot.requestPaint()
    onSelectedIndexChanged: plot.requestPaint()
    onWidthChanged: plot.requestPaint()
    onHeightChanged: plot.requestPaint()
    Connections {
        target: Commons.Color
        function onAccentChanged() { plot.requestPaint() }
    }
    Connections { target: Commons.Color.popups; function onTextChanged() { plot.requestPaint() } }
    Connections { target: I18n; function onLanguageChanged() { plot.requestPaint() } }
    PlainText {
        anchors.top: parent.top; anchors.right: parent.right
        text: root.selected ? root.time(root.selected.ts)+" · "+(root.selected[root.metric]>=0 ? I18n.formatNumber(root.selected[root.metric],root.metric==="charge" ? 0:1)+" "+root.unit : "—") : root.unit
        font.pixelSize: Style.font.caption
        color: Commons.Color.accent
    }
    Canvas {
        id: plot
        anchors.fill: parent
        onPaint: {
            let c=getContext("2d"); c.reset()
            let fg=Commons.Color.popups.text
            c.font=Math.round(Style.font.caption)+"px '"+Style.font.family+"'"
            c.fillStyle=fg
            for(let i=0;i<3;i++) {
                let v=root.ceiling*(1-i/2), y=root.positionY(v)
                c.strokeStyle=Qt.rgba(fg.r,fg.g,fg.b,.13)
                c.beginPath(); c.moveTo(root.plotLeft,y); c.lineTo(width,y); c.stroke()
                c.fillText(I18n.formatNumber(v,0),0,y+3)
            }
            if (!root.points.length) return
            c.fillText(root.time(root.points[0].ts),root.plotLeft,height-2)
            let end=root.time(root.points[root.points.length-1].ts)
            c.fillText(end,width-c.measureText(end).width,height-2)
            c.strokeStyle=Commons.Color.accent; c.lineWidth=2; c.beginPath()
            let previous=null
            for(let p of root.points) {
                if(p[root.metric] === undefined || p[root.metric]<0) { previous=null; continue }
                let x=root.positionX(p.ts), y=root.positionY(p[root.metric])
                if (!previous || p.ts-previous.ts>90) c.moveTo(x,y); else c.lineTo(x,y)
                previous=p
            }
            c.stroke()
            let s=root.selected
            if(s && s[root.metric]>=0) {
                c.strokeStyle=Qt.rgba(fg.r,fg.g,fg.b,.5)
                c.beginPath(); c.moveTo(root.positionX(s.ts),root.plotTop); c.lineTo(root.positionX(s.ts),root.plotBottom); c.stroke()
                c.fillStyle=Commons.Color.accent; c.beginPath(); c.arc(root.positionX(s.ts),root.positionY(s[root.metric]),3,0,Math.PI*2); c.fill()
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: function(mouse) { if(!root.pinned) root.selectAt(mouse.x) }
        onExited: if(!root.pinned) root.selectedIndex=-1
        onClicked: function(mouse) {
            root.selectAt(mouse.x); root.pinned=!root.pinned
            root.pinnedTimestamp=root.pinned && root.selected ? root.selected.ts:0
        }
    }
}
