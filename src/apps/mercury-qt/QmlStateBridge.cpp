#include "QmlStateBridge.h"
#include <QVariantMap>
#include <QDebug>

QmlStateBridge::QmlStateBridge(std::shared_ptr<eillance::view_model::MercuryViewModel> viewModel, QObject *parent)
    : QObject(parent), m_viewModel(std::move(viewModel)) {

    // Set the callback on the C++ ViewModel.
    // It will be invoked whenever the core state changes.
    m_viewModel->SetOnStateChangedCallback([this]() {
        // The callback is invoked from a non-GUI thread.
        // We must use QMetaObject::invokeMethod to safely trigger an update on the main GUI thread.
        QMetaObject::invokeMethod(this, &QmlStateBridge::onStateUpdated, Qt::QueuedConnection);
    });

    // Perform an initial state update to populate the UI.
    onStateUpdated();
}

void QmlStateBridge::onStateUpdated() {
    // This slot is safely executed on the main GUI thread.
    // It fetches the latest state from the C++ ViewModel and updates the QML-exposed properties.
    updateState(m_viewModel->GetState());
}

void QmlStateBridge::updateState(const eillance::view_model::UIState& newState) {
    // Lock the mutex to ensure thread-safe access to the member variables that QML reads.
    std::lock_guard<std::mutex> lock(m_mutex);

    bool changed = false;

    // --- Update My ID ---
    QString newId = QString::fromStdString(newState.my_id);
    if (m_myId != newId) {
        m_myId = newId;
        changed = true;
    }

    // --- Update Selected Contact ID ---
    QString newSelectedContactId = newState.selected_contact_id ? QString::fromStdString(*newState.selected_contact_id) : QString();
    if (m_selectedContactId != newSelectedContactId) {
        m_selectedContactId = newSelectedContactId;
        changed = true;
    }

    // --- Update Contacts List ---
    QVariantList newContacts;
    for (const auto& contact : newState.contacts) {
        QVariantMap contactMap;
        contactMap["peer_id"] = QString::fromStdString(contact.peer_id);
        contactMap["display_name"] = QString::fromStdString(contact.display_name);

        // This logic translates the C++ enums into human-readable strings and colors
        // for direct use by the QML delegate, keeping the UI layer clean.
        switch(contact.status) {
            case eillance::identity::ContactStatus::pendingApproval:
                contactMap["status_text"] = "Request Sent";
                contactMap["is_awaiting_approval"] = false;
                contactMap["status_color"] = "orange";
                break;
            case eillance::identity::ContactStatus::awaitingApproval:
                contactMap["status_text"] = "ACTION REQUIRED";
                contactMap["is_awaiting_approval"] = true;
                contactMap["status_color"] = "red";
                break;
            case eillance::identity::ContactStatus::confirmed:
                 switch(contact.connection_state) {
                    case eillance::transport::PeerConnectionState::Connected:
                        contactMap["status_text"] = "Online";
                        contactMap["status_color"] = "green";
                        break;
                    case eillance::transport::PeerConnectionState::Connecting:
                        contactMap["status_text"] = "Connecting...";
                        contactMap["status_color"] = "gray";
                        break;
                    default:
                        contactMap["status_text"] = "Offline";
                        contactMap["status_color"] = "gray";
                        break;
                }
                contactMap["is_awaiting_approval"] = false;
                break;
        }
        newContacts.append(contactMap);
    }

    if (m_contacts != newContacts) {
        m_contacts = newContacts;
        changed = true;
    }

    // --- Update Interaction History ---
    QVariantList newHistory;
    for(const auto& entry : newState.interaction_history) {
        // For now, we only handle text messages as per the QML implementation.
        // This can be expanded to handle call logs and system messages.
        if(entry.kind == eillance::messaging::HistoryEntryKind::textMessage) {
            QVariantMap entryMap;
            entryMap["content"] = QString::fromStdString(std::get<eillance::messaging::MessageEntry>(entry.data).content);
            entryMap["direction"] = (entry.direction == eillance::messaging::MessageDirection::sent) ? "sent" : "received";
            newHistory.append(entryMap);
        }
    }

    if(m_interactionHistory != newHistory) {
        m_interactionHistory = newHistory;
        changed = true;
    }


    // If any property changed, emit the signal to notify QML.
    if (changed) {
        emit stateChanged();
    }
}

// --- Property Getter Implementations ---

QString QmlStateBridge::myId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_myId;
}

QVariantList QmlStateBridge::contacts() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contacts;
}

QString QmlStateBridge::selectedContactId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedContactId;
}

QVariantList QmlStateBridge::interactionHistory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_interactionHistory;
}

// --- Invokable Method Implementations ---

void QmlStateBridge::selectContact(const QString& peerId) {
    // This method is called directly from QML. It forwards the request to the C++ ViewModel.
    // The resulting state change will be propagated back through the onStateUpdated callback mechanism.
    m_viewModel->SelectContact(peerId.toStdString());
}

void QmlStateBridge::deselectContact() {
    m_viewModel->DeselectContact();
}

void QmlStateBridge::addContact(const QString &peerId) {
    m_viewModel->AddContact(peerId.toStdString());
}

void QmlStateBridge::acceptContact(const QString &peerId) {
    m_viewModel->AcceptContact(peerId.toStdString());
}
