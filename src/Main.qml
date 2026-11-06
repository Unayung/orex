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

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            // Collapsed, the sidebar keeps a narrow strip with the button that
            // opens it again, so the toggle is always where you left it.
            property bool open: true
            Layout.preferredWidth: open ? 200 : 28
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
                    visible: sidebar.open
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
                    text: sidebar.open ? "‹" : "›"
                    font.pixelSize: 18
                    ToolTip.text: (sidebar.open ? qsTr("Hide sessions") : qsTr("Show sessions")) + "  (Ctrl+Shift+B)"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onClicked: sidebar.open = !sidebar.open
                }
            }

            ListView {
                anchors.fill: parent
                anchors.topMargin: sidebarHeader.height
                visible: sidebar.open
                model: backend.sessions
                // Sessions grouped by the machine they run on.
                section.property: "host_label"
                section.delegate: Label {
                    required property string section
                    width: ListView.view.width
                    topPadding: 12
                    bottomPadding: 4
                    leftPadding: 14
                    text: section
                    font.pixelSize: 11
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    opacity: 0.5
                }
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property string kind: modelData.kind ?? "session"
                    readonly property bool isPane: kind === "pane"
                    readonly property bool placeholder: kind === "placeholder"
                    readonly property bool current: modelData.session_id === backend.currentSession
                                                    && modelData.host === backend.currentHost
                    readonly property bool hasSubtitle: !isPane && (modelData.subtitle ?? "").length > 0
                    width: ListView.view.width
                    height: isPane ? 26 : (hasSubtitle ? 46 : 34)
                    color: current && !isPane ? theme.selection
                         : (hover.hovered && !placeholder ? Qt.lighter(theme.background, 1.3) : "transparent")

                    Rectangle {
                        width: 3
                        height: parent.height
                        color: Material.accent
                        visible: row.current && !row.isPane
                    }

                    // A program's status from OSC 7501, such as Claude Code
                    // working or waiting on you.
                    // Working breathes in the accent, blocked is a ringed red
                    // that waits for you, done is a still green.
                    Rectangle {
                        id: dot
                        readonly property string status: row.modelData.status ?? ""
                        visible: status.length > 0 && status !== "idle"
                        width: status === "blocked" ? 9 : 7
                        height: width
                        radius: width / 2
                        x: (row.isPane ? 26 : 182) - width / 2
                        y: row.isPane ? (row.height - height) / 2 : 15 - height / 2
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

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        x: row.isPane ? 36 : 14
                        width: parent.width - x - (row.isPane ? 8 : 24)
                        spacing: 1
                        Label {
                            width: parent.width
                            elide: Text.ElideRight
                            text: row.modelData.label
                            color: theme.foreground
                            font.italic: row.placeholder
                            font.pixelSize: row.isPane ? 12 : (row.placeholder ? 12 : 14)
                            font.bold: row.isPane && row.current && row.modelData.focused
                            opacity: row.placeholder ? 0.4
                                   : row.isPane ? (row.current && row.modelData.focused ? 0.95 : 0.6)
                                   : (row.current ? 1 : 0.65)
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
                    HoverHandler { id: hover }
                    ToolTip.visible: hover.hovered && (row.modelData.detail ?? row.modelData.label ?? "").length > 0
                                     && (row.placeholder || row.isPane)
                    ToolTip.text: row.modelData.detail ?? row.modelData.label ?? ""
                    ToolTip.delay: 600
                    TapHandler {
                        enabled: !row.placeholder
                        onTapped: row.isPane ? backend.focusPane(row.modelData.session_id, row.modelData.block_id)
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
                        required property var modelData
                        text: modelData.label
                        width: implicitWidth
                        onClicked: backend.selectWindow(modelData.window_id)
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
                            z: modelData.z + 0.7
                            color: lit ? Material.accent : Qt.lighter(theme.selection, 1.7)
                            border.width: 1
                            border.color: lit ? Material.accent : Qt.lighter(theme.selection, 2.4)

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
                            border.color: Qt.lighter(theme.selection, 1.6)
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
                            color: chipHover.hovered ? Qt.lighter(theme.selection, 1.4) : theme.selection
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
        background: Rectangle { color: Qt.lighter(theme.background, 1.2); radius: Math.min(8, theme.radius); border.color: theme.selection }

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

    // Every shortcut, as the Omarchy convention asks.
    Popup {
        id: help
        anchors.centerIn: Overlay.overlay
        width: Math.min(620, window.width - 80)
        height: Math.min(620, window.height - 80)
        modal: true
        Overlay.modal: Rectangle { color: "#99000000" }
        padding: 16
        background: Rectangle { color: Qt.lighter(theme.background, 1.2); radius: Math.min(8, theme.radius); border.color: theme.selection }
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
