import QtQuick
import Dayline 1.0

Canvas {
    id: root
    property string name: "calendar"
    property color color: Theme.secondary
    implicitWidth: 20
    implicitHeight: 20
    onColorChanged: requestPaint()
    onNameChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const p = getContext("2d")
        p.reset()
        p.scale(width / 24, height / 24)
        p.strokeStyle = color
        p.fillStyle = color
        p.lineWidth = 1.6
        p.lineCap = "round"
        p.lineJoin = "round"
        function line(x1,y1,x2,y2) { p.moveTo(x1,y1); p.lineTo(x2,y2) }
        function circle(x,y,r) { p.moveTo(x+r,y); p.arc(x,y,r,0,Math.PI*2) }
        p.beginPath()
        switch(name) {
        case "desktop":
            p.roundedRect(3,4,18,13,2,2); line(12,17,12,21); line(8,21,16,21); break
        case "lock":
            p.roundedRect(5,10,14,11,2,2); p.moveTo(8,10); p.lineTo(8,7); p.bezierCurveTo(8,1,16,1,16,7); p.lineTo(16,10); line(12,14,12,17); break
        case "calendar":
            p.roundedRect(4,5,16,16,3,3); line(4,10,20,10); line(8,3,8,7); line(16,3,16,7)
            line(8,14,10,14); line(14,14,16,14); line(8,17,10,17); break
        case "list":
            circle(5,6,0.6); circle(5,12,0.6); circle(5,18,0.6)
            line(10,6,20,6); line(10,12,20,12); line(10,18,20,18); break
        case "search": circle(10.5,10.5,6.5); line(15.5,15.5,20,20); break
        case "plus": line(12,5,12,19); line(5,12,19,12); break
        case "left": p.moveTo(14,6); p.lineTo(8,12); p.lineTo(14,18); break
        case "right": p.moveTo(10,6); p.lineTo(16,12); p.lineTo(10,18); break
        case "down": p.moveTo(6,9); p.lineTo(12,15); p.lineTo(18,9); break
        case "close": line(6,6,18,18); line(18,6,6,18); break
        case "check": p.moveTo(5,12); p.lineTo(10,17); p.lineTo(19,7); break
        case "clock": circle(12,12,8.5); p.moveTo(12,7); p.lineTo(12,12); p.lineTo(16,14); break
        case "sun":
            circle(12,12,4)
            for(let i=0;i<8;i++) { const a=i*Math.PI/4; line(12+7*Math.cos(a),12+7*Math.sin(a),12+9.5*Math.cos(a),12+9.5*Math.sin(a)) }
            break
        case "moon":
            p.moveTo(20,14); p.bezierCurveTo(15,22,4,19,4,11); p.bezierCurveTo(4,7,7,4,11,4)
            p.bezierCurveTo(7,11,13,17,20,14); break
        case "settings":
            circle(12,12,3)
            for(let i=0;i<8;i++) { const a=i*Math.PI/4; line(12+7*Math.cos(a),12+7*Math.sin(a),12+9*Math.cos(a),12+9*Math.sin(a)) }
            circle(12,12,7); break
        case "cloud":
            p.moveTo(7,18); p.bezierCurveTo(0,18,1,9,7,10); p.bezierCurveTo(8,2,20,4,19,12)
            p.bezierCurveTo(25,13,22,19,18,18); p.lineTo(7,18); break
        case "arrow": line(5,12,19,12); p.moveTo(14,7); p.lineTo(19,12); p.lineTo(14,17); break
        case "trash":
            line(4,7,20,7); p.moveTo(6,7); p.lineTo(7,21); p.lineTo(17,21); p.lineTo(18,7)
            p.moveTo(9,7); p.lineTo(9,3); p.lineTo(15,3); p.lineTo(15,7); line(10,11,10,17); line(14,11,14,17); break
        case "note":
            p.roundedRect(5,3,14,18,2,2); line(9,8,15,8); line(9,12,15,12); line(9,16,12,16); break
        }
        p.stroke()
    }
}
