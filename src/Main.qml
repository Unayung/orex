import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    visible: true
    title: "orex"
    color: theme.background
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius

    // Actions that need a dialog or only change this window run here; the
    // rest go to the backend, which turns them into Rex API calls.
    function run(id) {
        if (id === "palette") picker.show("actions")
        else if (id === "session.switch") picker.show("sessions")
        else if (id === "help") help.open()
        else if (id === "sidebar.toggle") sidebar.open = !sidebar.open
        else if (id === "host.add") addHost.begin()
        else if (id === "tab.rename") rename.ask(id, qsTr("Rename Tab"), backend.windows.find(w => w.window_id === backend.activeWindow)?.label ?? "")
        else if (id === "session.rename") rename.ask(id, qsTr("Rename Session"), backend.sessions.find(s => s.session_id === backend.currentSession)?.label ?? "")
        else backend.perform(id)
    }

    // One shortcut per action that has one, so the help, the palette, and
    // the keys can't disagree. Shortcuts see keys before the terminal does.
    Instantiator {
        model: backend.actions.filter(a => a.shortcut.length > 0)
        delegate: Shortcut {
            required property var modelData
            sequences: [modelData.shortcut]
            context: Qt.ApplicationShortcut
            onActivated: window.run(modelData.id)
        }
    }
    Shortcut { sequence: "Ctrl+Shift+]"; context: Qt.ApplicationShortcut; onActivated: backend.stepWindow(1) }
    Shortcut { sequence: "Ctrl+Shift+["; context: Qt.ApplicationShortcut; onActivated: backend.stepWindow(-1) }
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: "Alt+" + (index + 1)
            context: Qt.ApplicationShortcut
            onActivated: backend.perform("tab.goto", index + 1)
        }
    }

    // While the sidebar is hidden: a thin strip on the left edge that only
    // watches the pointer (clicks pass through to the panes), and the tab it
    // brings out to open the sidebar again.
    Item {
        id: edge
        visible: !sidebar.open
        z: 3000
        width: 6
        height: parent.height
        HoverHandler {
            id: edgeHover
            // Remember where along the edge the pointer is, so the tab comes
            // out right there. It stops following once the pointer is on the
            // tab itself, so it holds still to be clicked.
            onPointChanged: if (hovered && !tabHover.hovered) revealTab.pointerY = point.position.y
        }
    }
    Rectangle {
        id: revealTab
        readonly property bool shown: !sidebar.open && (edgeHover.hovered || tabHover.hovered)
        property real pointerY: 8
        z: 3001
        width: 26
        height: 40
        x: shown ? 0 : -width
        // Centered on the pointer, kept inside the window.
        y: Math.round(Math.max(4, Math.min(window.height - height - 4, pointerY - height / 2)))
        radius: Math.min(6, theme.radius)
        color: tabHover.hovered ? Material.accent : Qt.lighter(theme.surface, 1.3)
        border.color: Material.accent
        Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        Label {
            anchors.centerIn: parent
            text: "›"
            font.pixelSize: 18
            color: tabHover.hovered ? theme.accentForeground : theme.foreground
        }
        HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: sidebar.open = true }
        ToolTip.visible: tabHover.hovered
        ToolTip.text: qsTr("Show sessions") + "  (Ctrl+Shift+B)"
        ToolTip.delay: 400
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            // Collapsed, the sidebar takes no room at all; pointing at the
            // window's left edge brings out a tab that opens it again.
            property bool open: true
            visible: open
            Layout.preferredWidth: open ? 200 : 0
            Layout.fillHeight: true
            color: Qt.darker(theme.background, 1.25)
            clip: true

            Item {
                id: sidebarHeader
                width: parent.width
                height: 32

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    text: qsTr("Sessions")
                    font.pixelSize: 11
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    opacity: 0.5
                }
                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: 28
                    implicitHeight: 28
                    text: "‹"
                    font.pixelSize: 18
                    ToolTip.text: qsTr("Hide sessions") + "  (Ctrl+Shift+B)"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onClicked: sidebar.open = false
                }
            }

            ListView {
                anchors.fill: parent
                anchors.topMargin: sidebarHeader.height
                visible: sidebar.open
                model: backend.sessions
                // Sessions grouped by the machine they run on.
                section.property: "host_label"
                // Each machine's sessions start with a band of their own, so
                // where one host ends and the next begins is plain.
                section.delegate: Item {
                    required property string section
                    width: ListView.view.width
                    height: 42

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        height: 30
                        // A tenth of the text color over the sidebar: it shows on
                        // near-black themes, where lighter() has nothing to scale.
                        readonly property color fg: Qt.color(theme.foreground)
                        // Neutral on purpose: the accent means "selected" in this
                        // list, and a machine heading isn't something you select.
                        color: Qt.tint(sidebar.color, Qt.rgba(fg.r, fg.g, fg.b, 0.12))
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 14
                            width: parent.width - 20
                            elide: Text.ElideRight
                            text: parent.parent.section
                            font.pixelSize: 13
                            font.bold: true
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1.2
                            color: theme.foreground
                            opacity: 0.85
                        }
                    }
                }
                // Machine (the band above), then workspace, tab, and pane, each
                // set further in, with a guide line down each level it's under.
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property string kind: modelData.kind ?? "session"
                    readonly property bool isPane: kind === "pane"
                    readonly property bool isTab: kind === "tab"
                    readonly property bool isSession: kind === "session"
                    readonly property bool placeholder: kind === "placeholder"
                    // 1 workspace, 2 tab or a pane right under its workspace, 3 a pane under a tab.
                    readonly property int depth: isPane ? (modelData.depth ?? 2) : isTab ? 2 : 1
                    readonly property real indent: 14 + (depth - 1) * 16
                    readonly property bool current: modelData.session_id === backend.currentSession
                                                    && modelData.host === backend.currentHost
                    readonly property bool hasSubtitle: isSession && (modelData.subtitle ?? "").length > 0
                    readonly property bool inFront: (isTab || isPane) && current && (modelData.focused ?? false)
                    width: ListView.view.width
                    height: isSession ? (hasSubtitle ? 46 : 34) : 26
                    // The selected session is tinted with the accent, which no
                    // machine heading uses, so the two never look alike.
                    readonly property color accentTint: Qt.tint(sidebar.color, Qt.rgba(Material.accent.r, Material.accent.g,
                                                                                         Material.accent.b, 0.22))
                    color: current && isSession ? accentTint
                         : (hover.hovered && !placeholder ? Qt.lighter(theme.background, 1.3) : "transparent")

                    Rectangle {
                        width: 3
                        height: parent.height
                        color: Material.accent
                        visible: row.current && row.isSession
                    }

                    // Guide lines for the levels this row sits under.
                    Repeater {
                        model: row.depth - 1
                        Rectangle {
                            required property int index
                            x: 20 + index * 16
                            width: 1
                            height: row.height
                            color: theme.foreground
                            opacity: 0.15
                        }
                    }

                    // A program's status from OSC 7501, such as Claude Code
                    // working or waiting on you. Working breathes in the accent,
                    // blocked is a ringed red that waits for you, done is a
                    // still green. Every row keeps it in the same column on the
                    // right, left of the session's bell.
                    Rectangle {
                        id: dot
                        readonly property string status: row.modelData.status ?? ""
                        visible: status.length > 0 && status !== "idle"
                        width: status === "blocked" ? 9 : 7
                        height: width
                        radius: width / 2
                        x: row.width - 34 - width / 2
                        y: row.isSession ? 15 - height / 2 : (row.height - height) / 2
                        color: status === "blocked" ? theme.red : status === "done" ? theme.green : Material.accent
                        border.width: status === "blocked" ? 2 : 0
                        border.color: Qt.lighter(theme.red, 1.5)
                        SequentialAnimation on opacity {
                            running: dot.visible && dot.status === "working"
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.25; duration: 700; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
                            onRunningChanged: if (!running) dot.opacity = 1
                        }
                    }

                    // The coding agent in the pane, such as Claude Code or Hermes.
                    Image {
                        id: agentIcon
                        readonly property string url: row.modelData.icon ?? ""
                        visible: url.length > 0
                        source: url
                        width: 14
                        height: 14
                        sourceSize: Qt.size(28, 28)
                        smooth: true
                        x: row.indent + (row.isPane ? 4 : 0)
                        y: row.isSession ? 15 - height / 2 : (row.height - height) / 2
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        x: row.indent + (row.isPane ? 4 : 0) + (agentIcon.visible ? 19 : 0)
                        width: parent.width - x - 44 // clear of the status column and the bell
                        spacing: 1
                        Label {
                            width: parent.width
                            elide: Text.ElideRight
                            text: (row.isTab ? "▸ " : "") + row.modelData.label
                            color: theme.foreground
                            font.italic: row.placeholder
                            font.pixelSize: row.isSession ? 14 : 12
                            font.weight: row.isSession ? (row.current ? Font.Bold : Font.DemiBold)
                                                       : (row.inFront ? Font.Bold : Font.Normal)
                            opacity: row.placeholder ? 0.4
                                   : row.isSession ? (row.current ? 1 : 0.75)
                                   : row.inFront ? 0.95 : 0.6
                        }
                        Label {
                            visible: row.hasSubtitle
                            width: parent.width
                            elide: Text.ElideRight
                            text: row.modelData.subtitle ?? ""
                            color: theme.foreground
                            font.pixelSize: 11
                            opacity: 0.5
                        }
                    }
                    // Notifications for this session: a muted bell stays in
                    // view; the plain one shows on hover, to mute with a click.
                    Label {
                        id: bell
                        readonly property bool muted: row.modelData.muted ?? false
                        visible: row.isSession && (muted || hover.hovered)
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        y: 15 - height / 2
                        text: muted ? "🔕" : "🔔"
                        font.family: "Noto Color Emoji"
                        font.pixelSize: 12
                        opacity: muted ? 0.85 : (bellHover.hovered ? 0.9 : 0.35)
                        HoverHandler { id: bellHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: backend.toggleMute(row.modelData.host, row.modelData.session_id) }
                        ToolTip.visible: bellHover.hovered
                        ToolTip.text: muted ? qsTr("Notifications muted. Click to turn them on.") : qsTr("Mute notifications")
                        ToolTip.delay: 400
                    }

                    HoverHandler { id: hover }
                    ToolTip.visible: hover.hovered && (row.modelData.detail ?? row.modelData.label ?? "").length > 0
                                     && !row.isSession
                    ToolTip.text: row.modelData.detail ?? row.modelData.label ?? ""
                    ToolTip.delay: 600
                    TapHandler {
                        enabled: !row.placeholder
                        // A click on the bell only mutes; it doesn't switch sessions.
                        onTapped: bellHover.hovered ? undefined : row.isPane ? backend.focusPane(row.modelData.session_id, row.modelData.block_id)
                                : row.isTab ? backend.focusTab(row.modelData.session_id, row.modelData.window_id)
                                : backend.selectSession(row.modelData.session_id)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TabBar {
                id: tabs
                Layout.fillWidth: true
                visible: backend.windows.length > 1
                // The server says which tab is active. A Binding survives the
                // click that TabBar handles itself, so the server's answer
                // still moves the highlight.
                Binding on currentIndex {
                    value: backend.windows.findIndex(w => w.window_id === backend.activeWindow)
                    restoreMode: Binding.RestoreNone
                }
                Repeater {
                    model: backend.windows
                    TabButton {
                        id: tab
                        required property var modelData
                        readonly property string status: modelData.status ?? ""
                        text: modelData.label
                        width: implicitWidth + (status.length > 0 ? 14 : 0)
                        onClicked: backend.selectWindow(modelData.window_id)
                        // The tab's most urgent pane, so a tab you aren't on
                        // can still say it needs you.
                        Rectangle {
                            visible: tab.status.length > 0 && tab.status !== "idle"
                            width: tab.status === "blocked" ? 9 : 7
                            height: width
                            radius: width / 2
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            color: tab.status === "blocked" ? theme.red : tab.status === "done" ? theme.green : Material.accent
                            border.width: tab.status === "blocked" ? 2 : 0
                            border.color: Qt.lighter(theme.red, 1.5)
                            SequentialAnimation on opacity {
                                running: tab.status === "working"
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.25; duration: 700; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    anchors.fill: parent
                    color: theme.background // panes draw their own borders; gaps stay plain
                }

                Item {
                    id: paneArea
                    objectName: "paneArea"
                    anchors.fill: parent
                    anchors.margins: 4
                    // Room for the top row's drag handles, which sit across
                    // the panes' top edges.
                    anchors.topMargin: 9

                    // Dragging a pane by its handle: the pane under the pointer
                    // and the part of it the pane would go to.
                    property string dragFrom: ""
                    property string dropTarget: ""
                    property string dropZone: ""

                    function trackDrop(from, p) {
                        dragFrom = from
                        let best = null
                        for (const s of backend.paneSlots) {
                            if (s.block_id === from) continue
                            const inside = p.x >= s.x && p.x < s.x + s.width && p.y >= s.y && p.y < s.y + s.height
                            if (inside && (!best || s.z > best.z)) best = s
                        }
                        if (!best) { dropTarget = ""; return }
                        // The middle swaps the two panes; elsewhere the nearest
                        // edge says which side the pane goes on.
                        const fx = (p.x - best.x) / best.width, fy = (p.y - best.y) / best.height
                        let zone = "center"
                        if (fx < 0.3 || fx > 0.7 || fy < 0.3 || fy > 0.7) {
                            const d = { left: fx, right: 1 - fx, up: fy, down: 1 - fy }
                            zone = Object.keys(d).reduce((a, b) => d[a] <= d[b] ? a : b)
                        }
                        dropTarget = best.block_id
                        dropZone = zone
                        dropMarker.x = best.x + (zone === "right" ? best.width / 2 : 0)
                        dropMarker.y = best.y + (zone === "down" ? best.height / 2 : 0)
                        dropMarker.width = zone === "left" || zone === "right" ? best.width / 2 : best.width
                        dropMarker.height = zone === "up" || zone === "down" ? best.height / 2 : best.height
                    }
                    function finishDrop() {
                        if (dropTarget.length > 0)
                            backend.dropPane(dragFrom, dropTarget, dropZone)
                        dragFrom = ""
                        dropTarget = ""
                    }

                    // Each pane's title bar: its agent's icon and the title the
                    // program set, such as Claude Code's summary of the task.
                    Repeater {
                        model: backend.paneSlots
                        delegate: Item {
                            required property var modelData
                            // Inside the border at its widest, the 2px of a pane
                            // showing its program's status, so it never covers it.
                            x: modelData.x + 2
                            y: modelData.y + 2
                            width: modelData.width - 4
                            height: 20
                            z: modelData.z + 0.6
                            readonly property color fg: Qt.color(theme.foreground)

                            Rectangle {
                                anchors.fill: parent
                                color: Qt.tint(theme.background, Qt.rgba(parent.fg.r, parent.fg.g, parent.fg.b, 0.06))
                            }
                            Image {
                                id: headerIcon
                                visible: (modelData.icon ?? "").length > 0
                                source: modelData.icon ?? ""
                                x: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 12
                                height: 12
                                sourceSize: Qt.size(24, 24)
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: headerIcon.visible ? 26 : 8
                                // Stops short of the drag handle in the middle, which
                                // shows once there's more than one pane.
                                width: backend.paneSlots.length > 1 ? parent.width / 2 - x - 30 : parent.width - x - 8
                                elide: Text.ElideRight
                                text: modelData.title ?? ""
                                font.pixelSize: 11
                                font.bold: modelData.focused ?? false
                                color: theme.foreground
                                opacity: (modelData.focused ?? false) ? 0.9 : 0.55
                            }
                        }
                    }

                    // A handle on each pane's top edge, once there's more than
                    // one pane to arrange.
                    Repeater {
                        model: backend.paneSlots.length > 1 ? backend.paneSlots : []
                        delegate: Rectangle {
                            id: handle
                            required property var modelData
                            readonly property bool lit: grip.hovered || paneDrag.active
                            width: 48
                            height: 16
                            radius: height / 2
                            x: Math.round(modelData.x + modelData.width / 2 - width / 2)
                            y: Math.round(modelData.y - height / 2 + 1)
                            // Above the divider it sits on, so grabbing the grip
                            // moves the pane and the rest of the line resizes.
                            z: modelData.z + 0.9
                            color: lit ? Material.accent : Qt.lighter(theme.surface, 1.7)
                            border.width: 1
                            border.color: lit ? Material.accent : Qt.lighter(theme.surface, 2.4)

                            // A grip, so it reads as something to drag.
                            Label {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: -1
                                text: ":::"
                                font.pixelSize: 13
                                font.bold: true
                                font.letterSpacing: 1
                                color: handle.lit ? theme.accentForeground : theme.foreground
                                opacity: handle.lit ? 1 : 0.8
                            }

                            HoverHandler {
                                id: grip
                                cursorShape: paneDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            }
                            DragHandler {
                                id: paneDrag
                                target: null
                                onCentroidChanged: if (active) paneArea.trackDrop(handle.modelData.block_id,
                                                                                 handle.mapToItem(paneArea, centroid.position))
                                onActiveChanged: if (!active) paneArea.finishDrop()
                            }
                        }
                    }

                    // The borders between split panes: drag one to give either
                    // side more room. A line shows where it will go, and the
                    // server hears about it once, on release.
                    Repeater {
                        model: backend.dividers
                        delegate: Item {
                            id: divider
                            required property var modelData
                            readonly property bool vertical: modelData.vertical
                            property real dragAt: modelData.at
                            x: vertical ? Math.round(modelData.at - 4) : modelData.from
                            y: vertical ? modelData.from : Math.round(modelData.at - 4)
                            width: vertical ? 8 : modelData.length
                            height: vertical ? modelData.length : 8
                            z: modelData.z

                            HoverHandler {
                                id: borderHover
                                cursorShape: divider.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
                            }
                            DragHandler {
                                id: borderDrag
                                target: null
                                xAxis.enabled: divider.vertical
                                yAxis.enabled: !divider.vertical
                                onCentroidChanged: if (active) {
                                    const p = divider.mapToItem(paneArea, centroid.position)
                                    const lo = divider.modelData.start + divider.modelData.size * 0.1
                                    const hi = divider.modelData.start + divider.modelData.size * 0.9
                                    divider.dragAt = Math.max(lo, Math.min(hi, divider.vertical ? p.x : p.y))
                                }
                                onActiveChanged: if (!active) {
                                    backend.resizeSplit(divider.modelData.before, divider.modelData.after, divider.vertical,
                                                        (divider.dragAt - divider.modelData.start) / divider.modelData.size)
                                }
                            }
                            // Hover shows the border is grabbable; a drag shows
                            // where it's going.
                            Rectangle {
                                visible: borderHover.hovered || borderDrag.active
                                color: Material.accent
                                opacity: borderDrag.active ? 1 : 0.6
                                x: divider.vertical ? (borderDrag.active ? divider.dragAt - divider.x : 4) - 1 : 0
                                y: divider.vertical ? 0 : (borderDrag.active ? divider.dragAt - divider.y : 4) - 1
                                width: divider.vertical ? 2 : parent.width
                                height: divider.vertical ? parent.height : 2
                            }
                        }
                    }

                    // A pane's right-click menu. Its actions go to the pane it was
                    // opened on, not to whichever pane had focus.
                    Menu {
                        id: paneMenu
                        property string blockId
                        property bool hasSelection
                        property bool floating
                        property bool zoomed
                        z: 6000
                        background: Rectangle {
                            implicitWidth: 200
                            color: Qt.lighter(theme.background, 1.6)
                            border.color: theme.surface
                            radius: Math.min(6, theme.radius)
                        }
                        Connections {
                            target: backend
                            function onPaneMenuRequested(blockId, x, y, hasSelection, floating, zoomed) {
                                paneMenu.blockId = blockId
                                paneMenu.hasSelection = hasSelection
                                paneMenu.floating = floating
                                paneMenu.zoomed = zoomed
                                paneMenu.popup(paneArea, x, y)
                            }
                        }
                        function run(id) { backend.performOnPane(blockId, id) }
                        MenuItem { text: qsTr("Copy"); visible: paneMenu.hasSelection; height: visible ? implicitHeight : 0; onTriggered: paneMenu.run("pane.copy") }
                        MenuItem { text: qsTr("Paste"); onTriggered: paneMenu.run("pane.paste") }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Split Right"); onTriggered: paneMenu.run("pane.split.right") }
                        MenuItem { text: qsTr("Split Left"); onTriggered: paneMenu.run("pane.split.left") }
                        MenuItem { text: qsTr("Split Down"); onTriggered: paneMenu.run("pane.split.down") }
                        MenuItem { text: qsTr("Split Up"); onTriggered: paneMenu.run("pane.split.up") }
                        MenuSeparator {}
                        MenuItem { text: paneMenu.floating ? qsTr("Dock") : qsTr("Float"); onTriggered: paneMenu.run("pane.float") }
                        MenuItem { text: paneMenu.zoomed ? qsTr("Unzoom") : qsTr("Zoom"); onTriggered: paneMenu.run("pane.zoom") }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Close Pane"); onTriggered: paneMenu.run("pane.close") }
                    }

                    // Where the dragged pane would land.
                    Rectangle {
                        id: dropMarker
                        visible: paneArea.dropTarget.length > 0
                        z: 5000
                        radius: Math.min(4, theme.radius)
                        color: Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.22)
                        border.color: Material.accent
                        border.width: 2
                    }

                    // Frames of floating layers. They share this parent with
                    // the terminals, so their z interleaves with the panes'.
                    Repeater {
                        model: backend.frames
                        delegate: Rectangle {
                            id: frame
                            required property var modelData
                            x: modelData.x
                            y: modelData.y
                            width: modelData.width
                            height: modelData.height
                            z: modelData.z
                            color: theme.background
                            border.width: 1
                            // The panes inside draw their own borders, and the
                            // focused one shows the accent, so the frame stays quiet.
                            border.color: Qt.lighter(theme.surface, 1.6)
                            radius: Math.min(4, theme.radius)
                            // The terminals aren't children of the frame, so
                            // carry them along while it's dragged.
                            onXChanged: if (drag.active) backend.dragLayer(modelData.layer_id, x, y)
                            onYChanged: if (drag.active) backend.dragLayer(modelData.layer_id, x, y)

                            Item {
                                width: parent.width
                                height: 22

                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 8
                                    width: parent.width - 70
                                    elide: Text.ElideRight
                                    text: frame.modelData.title
                                    font.pixelSize: 12
                                    opacity: 0.8
                                }
                                Row {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    ToolButton {
                                        implicitWidth: 22; implicitHeight: 20
                                        text: "–"
                                        ToolTip.text: qsTr("Minimize")
                                        ToolTip.visible: hovered
                                        onClicked: backend.setLayerMinimized(frame.modelData.layer_id, true)
                                    }
                                    ToolButton {
                                        implicitWidth: 22; implicitHeight: 20
                                        text: "⊟"
                                        ToolTip.text: qsTr("Dock into the layout")
                                        ToolTip.visible: hovered
                                        onClicked: backend.perform("pane.float")
                                    }
                                }
                                // Drag by the title bar; the server gets the new
                                // place once, on release.
                                DragHandler {
                                    id: drag
                                    target: frame
                                    // Rex keeps floating layers inside the window,
                                    // so the drag stops at the edges too.
                                    xAxis.minimum: 0
                                    xAxis.maximum: paneArea.width - frame.width
                                    yAxis.minimum: 0
                                    yAxis.maximum: paneArea.height - frame.height
                                    onActiveChanged: if (!active) backend.moveLayer(frame.modelData.layer_id, frame.x, frame.y)
                                }
                                TapHandler { onTapped: backend.raiseLayer(frame.modelData.layer_id) }
                            }
                        }
                    }
                }

                // Minimized floating layers, to bring them back.
                Row {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: 10
                    spacing: 6
                    z: 1000
                    Repeater {
                        model: backend.minimized
                        delegate: Rectangle {
                            required property var modelData
                            width: chipLabel.implicitWidth + 24
                            height: 28
                            radius: Math.min(6, theme.radius)
                            color: chipHover.hovered ? Qt.lighter(theme.surface, 1.4) : theme.surface
                            border.color: Material.accent
                            Label {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: "▴ " + (parent.modelData.title || qsTr("Floating pane"))
                            }
                            HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: backend.setLayerMinimized(parent.modelData.layer_id, false) }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    z: 1001
                    visible: backend.status.length > 0
                    text: backend.status
                    color: theme.foreground
                    padding: 12
                    background: Rectangle { color: Qt.lighter(theme.background, 1.4); radius: Math.min(4, theme.radius) }
                }
            }
        }
    }

    // The command palette and the session switcher: type to filter, Enter
    // to run, Esc to close.
    Popup {
        id: picker
        property string mode: "actions"
        property var items: mode === "actions"
            ? backend.actions.filter(a => a.id !== "palette")
            : backend.sessions.filter(s => s.kind === "session")
                .map(s => ({ id: s.session_id, title: s.label, shortcut: s.host === "local" ? "" : s.host_label }))
        readonly property var shown: items.filter(i => i.title.toLowerCase().includes(query.text.toLowerCase()))

        function show(m) { mode = m; query.text = ""; list.currentIndex = 0; open(); query.forceActiveFocus() }
        function choose(item) {
            close()
            if (!item) return
            if (mode === "actions") window.run(item.id)
            else backend.selectSession(item.id)
        }

        anchors.centerIn: Overlay.overlay
        width: Math.min(560, window.width - 80)
        height: Math.min(440, window.height - 120)
        modal: true
        Overlay.modal: Rectangle { color: "#99000000" }
        padding: 8
        background: Rectangle { color: Qt.lighter(theme.background, 1.2); radius: Math.min(8, theme.radius); border.color: theme.surface }

        ColumnLayout {
            anchors.fill: parent
            TextField {
                id: query
                Layout.fillWidth: true
                placeholderText: picker.mode === "actions" ? qsTr("Run an action") : qsTr("Go to a session")
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onUpPressed: list.decrementCurrentIndex()
                onAccepted: picker.choose(picker.shown[list.currentIndex])
                onTextChanged: list.currentIndex = 0
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: picker.shown
                highlightMoveDuration: 0
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    contentItem: RowLayout {
                        Label { text: modelData.title; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: modelData.shortcut; opacity: 0.6; font.pixelSize: 12 }
                    }
                    onClicked: picker.choose(modelData)
                }
            }
        }
    }

    // Asks for a new name for a tab or session.
    Dialog {
        id: rename
        property string action
        function ask(id, heading, current) { action = id; title = heading; field.text = current; open(); field.selectAll(); field.forceActiveFocus() }
        anchors.centerIn: Overlay.overlay
        width: 380
        modal: true
        Overlay.modal: Rectangle { color: "#99000000" }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: backend.perform(action, field.text)
        TextField {
            id: field
            width: parent.width
            onAccepted: rename.accept()
        }
    }

    // Adds a Rex server on another machine, after checking it answers.
    Dialog {
        id: addHost
        property bool testing: false
        property string problem: ""
        function begin() { hostName.text = ""; hostAddress.text = ""; problem = ""; testing = false; open(); hostName.forceActiveFocus() }
        function submit() { if (testing) return; problem = ""; testing = true; backend.addRemoteHost(hostName.text, hostAddress.text) }
        title: qsTr("Add Remote Host")
        anchors.centerIn: Overlay.overlay
        width: 460
        modal: true
        Overlay.modal: Rectangle { color: "#99000000" }
        closePolicy: testing ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

        Connections {
            target: backend
            function onHostAddFinished(ok, message) {
                addHost.testing = false
                if (ok) addHost.close()
                else addHost.problem = message
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            TextField {
                id: hostName
                Layout.fillWidth: true
                placeholderText: qsTr("Name, such as m5")
                enabled: !addHost.testing
                onAccepted: hostAddress.forceActiveFocus()
            }
            TextField {
                id: hostAddress
                Layout.fillWidth: true
                placeholderText: qsTr("Address, such as https://mac.tail1234.ts.net")
                enabled: !addHost.testing
                onAccepted: addHost.submit()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                opacity: 0.6
                text: qsTr("A Rex server on your tailnet: its https:// address, or just the host name.")
            }
            Label {
                Layout.fillWidth: true
                visible: addHost.problem.length > 0
                wrapMode: Text.Wrap
                text: addHost.problem
                color: theme.red
            }
        }

        footer: DialogButtonBox {
            Button {
                text: addHost.testing ? qsTr("Connecting…") : qsTr("Add")
                enabled: !addHost.testing && hostName.text.trim().length > 0 && hostAddress.text.trim().length > 0
                onClicked: addHost.submit()
                BusyIndicator {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20
                    running: addHost.testing
                    visible: running
                }
            }
            Button {
                text: qsTr("Cancel")
                enabled: !addHost.testing
                onClicked: addHost.close()
            }
        }
    }

    // Every shortcut, as the Omarchy convention asks.
    Popup {
        id: help
        anchors.centerIn: Overlay.overlay
        width: Math.min(620, window.width - 80)
        height: Math.min(620, window.height - 80)
        modal: true
        Overlay.modal: Rectangle { color: "#99000000" }
        padding: 16
        background: Rectangle { color: Qt.lighter(theme.background, 1.2); radius: Math.min(8, theme.radius); border.color: theme.surface }
        ListView {
            anchors.fill: parent
            clip: true
            model: backend.actions.filter(a => a.shortcut.length > 0).concat([
                { title: qsTr("Go to Tab 1–9"), shortcut: "Alt+1…9" },
                { title: qsTr("Copy"), shortcut: "Ctrl+Shift+C" },
                { title: qsTr("Paste"), shortcut: "Ctrl+Shift+V, Shift+Insert" },
                { title: qsTr("Scroll back a page"), shortcut: "Shift+PageUp" }])
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: 28
                Label { text: modelData.title; Layout.fillWidth: true }
                Label { text: modelData.shortcut; color: Material.accent; font.family: "monospace" }
            }
        }
    }
}
