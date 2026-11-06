import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Orex

ApplicationWindow {
    id: window
    width: 1100
    height: 700
    visible: true
    title: "orex"
    color: "#101010"
    Material.theme: Material.Dark

    property string status: qsTr("Connecting to Rex…")

    TerminalItem {
        id: terminal
        objectName: "terminal"
        anchors.fill: parent
        anchors.margins: 6
        focus: true
    }

    Label {
        anchors.centerIn: parent
        visible: window.status.length > 0
        text: window.status
        color: "#c0c0c0"
        padding: 12
        background: Rectangle { color: "#202020"; radius: 4 }
    }
}
