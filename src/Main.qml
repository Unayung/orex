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
    color: "#101010"
    Material.theme: Material.Dark
    Material.accent: "#FFD60A"

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
            color: "#161616"

            ListView {
                anchors.fill: parent
                anchors.topMargin: 8
                model: backend.sessions
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool current: modelData.session_id === backend.currentSession
                    width: ListView.view.width
                    height: 36
                    color: current ? "#262626" : (hover.hovered ? "#1e1e1e" : "transparent")

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
                        color: parent.current ? "#ffffff" : "#a0a0a0"
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
                    color: "#2a2a2a" // shows through the gaps between panes
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
                    color: "#c0c0c0"
                    padding: 12
                    background: Rectangle { color: "#202020"; radius: 4 }
                }
            }
        }
    }
}
