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
        else if (id === "sidebar.toggle") sidebar.visible = !sidebar.visible
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
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: Qt.darker(theme.background, 1.25)

            ListView {
                anchors.fill: parent
                anchors.topMargin: 4
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
                    required property var modelData
                    readonly property bool current: modelData.session_id === backend.currentSession
                                                    && modelData.host === backend.currentHost
                    readonly property bool placeholder: modelData.placeholder === true
                    width: ListView.view.width
                    height: 34
                    color: current ? theme.selection
                         : (hover.hovered && !placeholder ? Qt.lighter(theme.background, 1.3) : "transparent")

                    Rectangle {
                        width: 3
                        height: parent.height
                        color: Material.accent
                        visible: parent.current
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 14
                        width: parent.width - 20
                        elide: Text.ElideRight
                        text: modelData.label
                        color: theme.foreground
                        font.italic: parent.placeholder
                        font.pixelSize: parent.placeholder ? 12 : 14
                        opacity: parent.current ? 1 : (parent.placeholder ? 0.4 : 0.65)
                    }
                    HoverHandler { id: hover }
                    ToolTip.visible: hover.hovered && (modelData.detail ?? "").length > 0
                    ToolTip.text: modelData.detail ?? ""
                    ToolTip.delay: 500
                    TapHandler { enabled: !parent.placeholder; onTapped: backend.selectSession(modelData.session_id) }
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
            : backend.sessions.filter(s => !s.placeholder)
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
