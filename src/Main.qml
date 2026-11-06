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

    // Ctrl+Shift chords, so plain keys and Ctrl keys still reach the program
    // in the terminal.
    Shortcut { sequence: "Ctrl+Shift+]"; context: Qt.ApplicationShortcut; onActivated: backend.stepWindow(1) }
    Shortcut { sequence: "Ctrl+Shift+["; context: Qt.ApplicationShortcut; onActivated: backend.stepWindow(-1) }
    Shortcut { sequence: "Ctrl+Shift+Down"; context: Qt.ApplicationShortcut; onActivated: backend.stepSession(1) }
    Shortcut { sequence: "Ctrl+Shift+Up"; context: Qt.ApplicationShortcut; onActivated: backend.stepSession(-1) }
    Shortcut { sequence: "Ctrl+Shift+B"; context: Qt.ApplicationShortcut; onActivated: sidebar.visible = !sidebar.visible }

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
                anchors.topMargin: 8
                model: backend.sessions
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool current: modelData.session_id === backend.currentSession
                    width: ListView.view.width
                    height: 36
                    color: current ? theme.selection : (hover.hovered ? Qt.lighter(theme.background, 1.3) : "transparent")

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
                        opacity: parent.current ? 1 : 0.65
                    }
                    HoverHandler { id: hover }
                    TapHandler { onTapped: backend.selectSession(modelData.session_id) }
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
                    color: theme.selection // shows through the gaps between panes
                }

                Item {
                    objectName: "paneArea"
                    anchors.fill: parent
                    anchors.margins: 4
                }

                Label {
                    anchors.centerIn: parent
                    visible: backend.status.length > 0
                    text: backend.status
                    color: theme.foreground
                    padding: 12
                    background: Rectangle { color: Qt.lighter(theme.background, 1.4); radius: Math.min(4, theme.radius) }
                }
            }
        }
    }
}
