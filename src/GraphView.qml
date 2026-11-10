import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The concepts the local model found in the library, related when one passage
// names both: a map to pan and zoom, a search, and an inspector with the
// concept's writeups in time order and its neighbours with the passage that
// relates them. All of it is the model's reading, and says so.
Item {
    id: view

    signal writeupChosen(var item)

    readonly property var graph: backend.graph
    readonly property var nodes: graph.nodes || []
    readonly property var edges: graph.edges || []
    property string selected: ""
    property string filter: ""
    // Re-read after every change to the graph.
    readonly property var info: {
        graph
        return selected ? backend.conceptInfo(selected) : ({})
    }
    readonly property var degree: {
        const d = {}
        for (const e of edges) {
            d[e.from] = (d[e.from] || 0) + 1
            d[e.to] = (d[e.to] || 0) + 1
        }
        return d
    }
    readonly property var neighbourIds: {
        const ids = {}
        for (const n of (info.neighbours || [])) ids[n.id] = true
        return ids
    }
    readonly property var matchedIds: {
        const ids = {}
        if (filter.trim()) for (const n of matches) ids[n.id] = true
        return ids
    }
    readonly property var matches: {
        const words = filter.trim().toLowerCase()
        const list = nodes.filter(n => !words || n.name.toLowerCase().indexOf(words) >= 0 || n.id.indexOf(words) >= 0)
        return list.sort((a, b) => b.urls.length - a.urls.length || (degree[b.id] || 0) - (degree[a.id] || 0)
                                    || a.name.localeCompare(b.name))
    }

    onGraphChanged: if (selected && !nodes.some(n => n.id === selected)) selected = ""

    function kindName(kind) {
        translator.language
        return kind === "technique" ? qsTr("technique") : kind === "tool" ? qsTr("tool")
             : kind === "vulnerability" ? qsTr("vulnerability") : kind === "technology" ? qsTr("technology") : qsTr("concept")
    }
    function choose(id) {
        selected = id
        const node = nodes.find(n => n.id === id)
        if (node) map.centerOn(node)
    }
    function emptyText() {
        translator.language
        if (!graph.total) return qsTr("No writeups yet, so there is nothing to map.")
        if (tutor.status === "battery")
            return qsTr("The local model is off while the laptop runs on battery. The map fills in once it is plugged in.")
        if (tutor.status === "missing")
            return qsTr("The map is drawn by a local model (Ollama) that reads your writeups. Install Ollama and run `ollama pull %1` to see it; search and the library work without it.").arg(tutor.model)
        if (tutor.status === "noModel")
            return qsTr("The map is drawn by the local model %1, which isn't downloaded yet. Run `ollama pull %1` to see it.").arg(tutor.model)
        return qsTr("The local model is reading your writeups. Concepts appear here as each one is done, a few minutes per writeup.")
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The map
        Item {
            id: map
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // The layout is square; it sits in the middle, below the heading,
            // with room on the right for the labels.
            readonly property real side: Math.max(100, Math.min(width - 220, height - 190))
            function px(node) { return (width - 100 - side) / 2 + 40 + node.x * side }
            function py(node) { return 110 + (height - 170 - side) / 2 + node.y * side }
            function centerOn(node) {
                world.x = width / 2 - px(node) * world.scale
                world.y = height / 2 - py(node) * world.scale
            }

            Column {
                x: 40; y: 34
                z: 2
                spacing: 4
                Text {
                    text: qsTr("Concepts")
                    color: Style.ink
                    font.family: Style.sans
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: view.nodes.length > 0
                    text: {
                        translator.language
                        const n = (x) => x.toLocaleString(Qt.locale(), 'f', 0)
                        return qsTr("Concepts: %1  ·  relations: %2  ·  writeups read by the model: %3 of %4").arg(n(view.nodes.length))
                               .arg(n(view.edges.length)).arg(n(view.graph.done || 0)).arg(n(view.graph.total || 0))
                    }
                    color: Style.muted
                    font.family: Style.sans
                    font.pixelSize: 13
                }
            }

            Text {
                anchors.centerIn: parent
                width: Math.min(parent.width - 120, 520)
                visible: view.nodes.length === 0
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: view.emptyText()
                color: Style.muted
                font.family: Style.sans
                font.pixelSize: 15
                lineHeight: 1.3
            }

            DragHandler {
                target: world
                cursorShape: Qt.ClosedHandCursor
            }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    const factor = Math.pow(1.0015, event.angleDelta.y)
                    const next = Math.max(0.4, Math.min(5, world.scale * factor))
                    const p = point.position
                    world.x = p.x - (p.x - world.x) * next / world.scale
                    world.y = p.y - (p.y - world.y) * next / world.scale
                    world.scale = next
                }
            }
            TapHandler { onTapped: view.selected = "" }

            Item {
                id: world
                width: map.width
                height: map.height
                transformOrigin: Item.TopLeft

                Canvas {
                    id: lines
                    anchors.fill: parent
                    property var index: {
                        const at = {}
                        for (const n of view.nodes) at[n.id] = n
                        return at
                    }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        for (const e of view.edges) {
                            const a = index[e.from], b = index[e.to]
                            if (!a || !b) continue
                            const lit = view.selected && (e.from === view.selected || e.to === view.selected)
                            ctx.strokeStyle = lit ? Style.accent : Style.line
                            ctx.globalAlpha = view.selected && !lit ? 0.35 : 1
                            ctx.lineWidth = Math.min(4, 0.8 + 0.7 * e.weight) / world.scale
                            ctx.beginPath()
                            ctx.moveTo(map.px(a), map.py(a))
                            ctx.lineTo(map.px(b), map.py(b))
                            ctx.stroke()
                        }
                    }
                    Connections {
                        target: view
                        function onEdgesChanged() { lines.requestPaint() }
                        function onSelectedChanged() { lines.requestPaint() }
                    }
                    Connections {
                        target: world
                        function onScaleChanged() { lines.requestPaint() }
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                }

                Repeater {
                    model: view.nodes
                    Item {
                        id: node
                        required property var modelData
                        readonly property bool chosen: view.selected === modelData.id
                        readonly property bool near: view.neighbourIds[modelData.id] === true
                        readonly property bool found: view.matchedIds[modelData.id] === true
                        readonly property real r: (5 + 3 * Math.sqrt(modelData.urls.length)) / world.scale
                        // Every name while there are few; in a big map, the main ones until zoomed in.
                        readonly property bool labelled: chosen || near || found || nodeHover.hovered
                                                         || (!view.selected && (view.nodes.length <= 60 || modelData.urls.length > 1
                                                                                || (view.degree[modelData.id] || 0) >= 3 || world.scale > 1.8))
                        x: map.px(modelData)
                        y: map.py(modelData)
                        z: chosen ? 3 : near || nodeHover.hovered ? 2 : 1
                        opacity: !view.selected || chosen || near ? 1 : 0.3

                        Rectangle {
                            x: -node.r; y: -node.r
                            width: 2 * node.r; height: width; radius: width / 2
                            color: Style.kindColor(node.modelData.kind)
                            border.color: node.chosen ? Style.ink : node.found ? Style.accent : "transparent"
                            border.width: 2 / world.scale
                        }
                        Text {
                            x: node.r + 4 / world.scale
                            y: -height / 2
                            visible: node.labelled
                            text: node.modelData.name
                            textFormat: Text.PlainText
                            color: node.chosen ? Style.ink : Style.muted
                            font.family: Style.sans
                            font.pixelSize: 12 / world.scale
                            font.weight: node.chosen ? Font.DemiBold : Font.Normal
                            style: Text.Outline
                            styleColor: Style.bg
                        }
                        Item {
                            x: -Math.max(node.r, 8 / world.scale); y: x
                            width: -2 * x; height: width
                            HoverHandler { id: nodeHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: view.selected = node.chosen ? "" : node.modelData.id }
                        }
                    }
                }
            }

            // Legend, and how to move around.
            Rectangle {
                anchors.fill: legend
                anchors.margins: -8
                radius: Style.radius
                color: Style.bg
                opacity: 0.9
                visible: legend.visible
            }
            Row {
                id: legend
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 22
                spacing: 16
                visible: view.nodes.length > 0
                Repeater {
                    model: ["concept", "technique", "tool", "vulnerability", "technology"]
                    Row {
                        required property string modelData
                        spacing: 6
                        Rectangle { width: 9; height: 9; radius: 5; anchors.verticalCenter: parent.verticalCenter; color: Style.kindColor(modelData) }
                        Text { text: view.kindName(modelData); color: Style.faint; font.family: Style.sans; font.pixelSize: 12 }
                    }
                }
                Text { text: qsTr("Drag to move, scroll to zoom"); color: Style.faint; font.family: Style.sans; font.pixelSize: 12 }
            }
        }

        // Search and inspector
        Rectangle {
            visible: view.nodes.length > 0
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(380, view.width * 0.36)
            color: Style.pane
            Rectangle { width: 1; height: parent.height; color: Style.line }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                TextField {
                    id: conceptSearch
                    Layout.fillWidth: true
                    placeholderText: qsTr("Find a concept")
                    onTextChanged: { view.filter = text; if (text) view.selected = "" }
                    Keys.onReturnPressed: if (view.matches.length) view.choose(view.matches[0].id)
                }

                // Every concept, most-read first, while none is picked.
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !view.selected
                    clip: true
                    model: view.matches
                    spacing: 2
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 34
                        radius: Style.radius
                        color: rowHover.hovered ? Style.hover : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10
                            Rectangle { width: 9; height: 9; radius: 5; color: Style.kindColor(modelData.kind) }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: Style.ink
                                font.family: Style.sans
                                font.pixelSize: 14
                            }
                            Text {
                                text: modelData.urls.length.toLocaleString(Qt.locale(), 'f', 0)
                                color: Style.faint
                                font.family: Style.sans
                                font.pixelSize: 12
                            }
                        }
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: view.choose(modelData.id) }
                    }
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !!view.selected && view.info.id !== undefined
                    contentHeight: inspector.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: inspector
                        width: parent.width
                        spacing: 10

                        Text {
                            text: "← " + qsTr("All concepts")
                            color: backHover.hovered ? Style.ink : Style.muted
                            font.family: Style.sans
                            font.pixelSize: 13
                            HoverHandler { id: backHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: view.selected = "" }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: view.info.name || ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                const parts = [view.kindName(view.info.kind)]
                                if ((view.info.aliases || []).length) parts.push(qsTr("also %1").arg(view.info.aliases.join(", ")))
                                return parts.join("  ·  ")
                            }
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Style.kindColor(view.info.kind)
                            font.family: Style.sans
                            font.pixelSize: 13
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Found by the local model %1, which can be wrong. Two concepts are related when a passage names both.")
                                  .arg((view.info.models || []).join(", "))
                            wrapMode: Text.Wrap
                            color: Style.faint
                            font.family: Style.sans
                            font.pixelSize: 12
                        }

                        Text {
                            Layout.topMargin: 8
                            text: qsTr("In your writeups")
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        // A timeline: the first time it came up, and every time since.
                        Repeater {
                            model: view.info.writeups || []
                            RowLayout {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 10
                                Item {
                                    Layout.preferredWidth: 10
                                    Layout.fillHeight: true
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 1
                                        y: index === 0 ? parent.height / 2 : 0
                                        height: (view.info.writeups.length === 1 ? 0 : index === 0 || index === view.info.writeups.length - 1 ? 0.5 : 1) * parent.height + 10
                                        color: Style.line
                                    }
                                    Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 5; color: Style.accent }
                                }
                                Column {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 4
                                    Layout.bottomMargin: 4
                                    spacing: 2
                                    Text {
                                        text: win.dayLabel(modelData.date)
                                        color: Style.faint
                                        font.family: Style.sans
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.title
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                        color: titleHover.hovered ? Style.accent : Style.ink
                                        font.family: Style.sans
                                        font.pixelSize: 14
                                        HoverHandler { id: titleHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: view.writeupChosen(modelData) }
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.topMargin: 8
                            text: qsTr("Related concepts")
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: (view.info.neighbours || []).length === 0
                            text: qsTr("No passage names it together with another concept.")
                            wrapMode: Text.Wrap
                            color: Style.muted
                            font.family: Style.sans
                            font.pixelSize: 13
                        }
                        Repeater {
                            model: view.info.neighbours || []
                            ColumnLayout {
                                id: neighbour
                                required property var modelData
                                property bool open: false
                                Layout.fillWidth: true
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Rectangle { width: 9; height: 9; radius: 5; color: Style.kindColor(neighbour.modelData.kind) }
                                    Text {
                                        Layout.fillWidth: true
                                        text: neighbour.modelData.name
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        color: nameHover.hovered ? Style.accent : Style.ink
                                        font.family: Style.sans
                                        font.pixelSize: 14
                                        HoverHandler { id: nameHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: view.choose(neighbour.modelData.id) }
                                    }
                                    Text {
                                        text: (neighbour.open ? "▴ " : "▾ ") + qsTr("%n passages", "", neighbour.modelData.weight)
                                        color: whyHover.hovered ? Style.ink : Style.faint
                                        font.family: Style.sans
                                        font.pixelSize: 12
                                        HoverHandler { id: whyHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: neighbour.open = !neighbour.open }
                                    }
                                }
                                Repeater {
                                    model: neighbour.open ? neighbour.modelData.evidence : []
                                    Rectangle {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 17
                                        implicitHeight: evidenceText.implicitHeight + 20
                                        radius: Style.radius
                                        color: Style.raised
                                        Text {
                                            id: evidenceText
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            text: (modelData.text ? modelData.text : qsTr("Both are named in the summary."))
                                                  + "\n— " + (modelData.title || modelData.url)
                                            textFormat: Text.PlainText
                                            wrapMode: Text.Wrap
                                            color: Style.muted
                                            font.family: Style.serif
                                            font.pixelSize: 13
                                            lineHeight: 1.25
                                        }
                                    }
                                }
                            }
                        }
                        Item { Layout.preferredHeight: 10 }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: !view.selected && view.nodes.length > 0
                    text: qsTr("Concepts and relations are the local model's reading of your writeups, not facts.")
                    wrapMode: Text.Wrap
                    color: Style.faint
                    font.family: Style.sans
                    font.pixelSize: 12
                }
            }
        }
    }
}
