import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    focus: true
    title: "Add Contact"

    contentItem: ColumnLayout {
        TextField {
            id: peerIdField
            placeholderText: "Enter Peer ID"
            Layout.fillWidth: true
        }
    }

    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
        onAccepted: {
            root.accept()
        }
    }
}
