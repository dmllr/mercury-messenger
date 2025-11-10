import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: contactListView

    // Custom property to receive the model from the parent
    property var contactsModel: []
    model: contactsModel

    // Custom signal to notify the parent when a contact is clicked
    signal contactClicked(string peerId)
    // Signal emitted when the accept button is clicked for a contact
    signal acceptClicked(string peerId)

    delegate: ItemDelegate {
        width: parent.width
        icon.source: "qrc:/qt-project.org/imports/QtQuick/Controls/icons/user.svg"

        contentItem: RowLayout {
            Label {
                text: modelData.display_name
                font.pixelSize: 16
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: modelData.status_text
                font.pixelSize: 12
                color: modelData.status_color
            }

            Button {
                text: "Accept"
                visible: modelData.is_awaiting_approval
                onClicked: (mouse) => {
                    // Emit the signal to be handled by the ListView
                    acceptClicked(modelData.peer_id)
                    // Prevent the ItemDelegate's onClicked from firing
                    mouse.accepted = true
                }
            }
        }

        onClicked: {
            // Only navigate if the contact is confirmed.
            if (!modelData.is_awaiting_approval) {
                contactClicked(modelData.peer_id)
            }
        }
    }

    onAcceptClicked: (peerId) => {
        // Handle the accept action by calling the C++ bridge
        StateBridge.acceptContact(peerId)
    }

    ScrollIndicator.vertical: ScrollIndicator { }

    Label {
        id: emptyLabel
        text: "No contacts. Add one to begin."
        anchors.centerIn: parent
        visible: contactListView.count === 0
        color: "gray"
    }
}
