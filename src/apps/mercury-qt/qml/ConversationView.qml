import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    // Properties to receive data from the parent
    property var conversationModel: []
    property string contactName: "Contact"

    // Signal to go back to the contact list
    signal backRequested()

    // Header
    Pane {
        Layout.fillWidth: true

        RowLayout {
            anchors.fill: parent
            anchors.margins: 5

            Button {
                icon.source: "qrc:/qt-project.org/imports/QtQuick/Controls/icons/arrow-left.svg"
                onClicked: backRequested()
                flat: true
            }

            Label {
                text: root.contactName
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    // Message list
    ListView {
        id: messageView
        Layout.fillWidth: true
        Layout.fillHeight: true

        model: root.conversationModel
        spacing: 10

        // This makes the list display from the bottom up, which is natural for a chat.
        contentY: contentHeight - height

        delegate: Rectangle {
            width: parent.width
            height: childrenRect.height
            color: "transparent"

            Rectangle {
                width: messageLabel.implicitWidth + 20
                height: messageLabel.implicitHeight + 10
                radius: 8

                // Position bubble left for received, right for sent
                anchors.right: modelData.direction === "sent" ? parent.right : undefined
                anchors.left: modelData.direction === "received" ? parent.left : undefined
                anchors.rightMargin: 10
                anchors.leftMargin: 10

                // Color bubble differently based on direction
                color: modelData.direction === "sent" ? "#dcf8c6" : "#ffffff"
                border.color: "#e0e0e0"

                Label {
                    id: messageLabel
                    text: modelData.content
                    anchors.centerIn: parent
                }
            }
        }
    }
}
