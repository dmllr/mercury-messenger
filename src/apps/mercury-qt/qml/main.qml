import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import mercury 1.0
import "."

ApplicationWindow {
    id: root
    visible: true
    width: 400
    height: 700
    title: "Mercury"

    header: ToolBar {
        Label {
            text: "My ID: " + StateBridge.myId
            anchors.centerIn: parent
            font.family: "monospace"
        }
    }

    AddContactDialog {
        id: addContactDialog
        // The built-in onAccepted signal is used here
        onAccepted: StateBridge.addContact(contentItem.children[0].text)

    }

    StackLayout {
        id: stack
        anchors.fill: parent

        ContactListView {
            id: contactList
            contactsModel: StateBridge.contacts

            // This syntax is correct when the module is properly imported
            onContactClicked: (peerId) => {
                StateBridge.selectContact(peerId)
                stack.push(conversationView)
            }
        }

        ConversationView {
            id: conversationView
            conversationModel: StateBridge.interactionHistory

            property var currentContact: {
                var contacts = StateBridge.contacts
                for (var i = 0; i < contacts.length; i++) {
                    if (contacts[i].peer_id === StateBridge.selectedContactId) {
                        return contacts[i]
                    }
                }
                return null
            }

            contactName: currentContact ? currentContact.display_name : ""

            onBackRequested: () => {
                stack.pop()
                StateBridge.deselectContact()
            }
        }
    }

    RoundButton {
        id: fab
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        text: "+"
        onClicked: addContactDialog.open()
        visible: stack.currentIndex === 0 // Show only on contact list
    }
}
