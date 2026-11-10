import QtQuick
import QtQuick.Shapes

// A line icon drawn from a 24×24 path: crisp at any size, in any colour,
// the same on every machine (font glyphs like ☰ or ⚙ came from whatever
// font had them).
Item {
    id: icon

    property string name
    property color color: Style.muted
    property real size: 20
    property bool filled: false
    property real weight: 1.8

    implicitWidth: size
    implicitHeight: size

    readonly property var paths: ({
        "days": "M4 7a2.5 2.5 0 0 1 2.5-2.5h11A2.5 2.5 0 0 1 20 7v11a2.5 2.5 0 0 1-2.5 2.5h-11A2.5 2.5 0 0 1 4 18Z M4 9.5h16 M8.5 2.5v4 M15.5 2.5v4 M8 13.5h3 M8 17h6",
        "star": "M12 3.6 L14.35 9.56 L20.75 9.96 L15.8 14.04 L17.41 20.24 L12 16.8 L6.59 20.24 L8.2 14.04 L3.25 9.96 L9.65 9.56 Z",
        "review": "M19.5 12a7.5 7.5 0 1 1-2.2-5.3 M19.5 4v4.5H15 M12 8.5V12l2.5 1.5",
        "stats": "M4 20h16 M6.5 20v-6 M11 20V6.5 M15.5 20v-9 M20 20V9.5",
        "graph": "M8.5 6.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0 M20.5 8.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0 M14.5 17.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0 M8.4 7.2l7.2 1 M7.2 8.7l3.6 6.6 M16.8 10.6l-3.7 4.9",
        "settings": "M4 7h9 M17 7h3 M4 17h3 M11 17h9 M17 7a2 2 0 1 1-4 0a2 2 0 1 1 4 0 M11 17a2 2 0 1 1-4 0a2 2 0 1 1 4 0",
        "help": "M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0 M9.5 9.5a2.5 2.5 0 1 1 3.6 2.25c-.65.32-1.1.95-1.1 1.7v.55 M12 17.2v.05",
        "left": "M14.5 6l-6 6 6 6",
        "right": "M9.5 6l6 6-6 6",
        "more": "M6 12h.01 M12 12h.01 M18 12h.01",
        "refresh": "M19.5 12a7.5 7.5 0 1 1-2.2-5.3 M19.5 4v4.5H15",
        "close": "M6 6l12 12 M18 6L6 18",
        "check": "M5 12.5l4.5 4.5L19 7.5",
        "notes": "M6.5 3.5h7.5l4.5 4.5v10a2.5 2.5 0 0 1-2.5 2.5h-9.5A2.5 2.5 0 0 1 4 18V6a2.5 2.5 0 0 1 2.5-2.5Z M13.5 3.5V8.5h5 M8 13h8 M8 16.5h5"
    })

    Shape {
        width: 24
        height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.name === "more" ? 2.6 : icon.weight
            fillColor: icon.filled ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
