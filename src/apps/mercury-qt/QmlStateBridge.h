
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <memory>
#include <mutex>

#include "eillance/view_model/mercury_view_model.hpp"

// The QmlStateBridge class acts as the intermediary between the C++ ViewModel and the QML user interface.
// It inherits from QObject to leverage Qt's Meta-Object System for properties, signals, and invokable methods.
// This is the standard pattern for exposing C++ logic to a QML frontend.
class QmlStateBridge : public QObject {
    Q_OBJECT

    // Expose the user's ID to QML. It's read-only from the QML side.
    // The NOTIFY signal is emitted whenever the value changes, allowing declarative bindings in QML to update automatically.
    Q_PROPERTY(QString myId READ myId NOTIFY stateChanged)

    // Expose the list of contacts. QVariantList is used to pass a list of QVariantMaps (like JSON objects) to QML.
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY stateChanged)
    
    // Expose the ID of the currently selected contact.
    Q_PROPERTY(QString selectedContactId READ selectedContactId NOTIFY stateChanged)
    
    // Expose the message history for the selected contact.
    Q_PROPERTY(QVariantList interactionHistory READ interactionHistory NOTIFY stateChanged)

public:
    // The constructor takes the C++ ViewModel as a dependency.
    explicit QmlStateBridge(std::shared_ptr<eillance::view_model::MercuryViewModel> viewModel, QObject *parent = nullptr);
    ~QmlStateBridge() override = default;

    // --- Read-only properties accessible from QML ---
    QString myId() const;
    QVariantList contacts() const;
    QString selectedContactId() const;
    QVariantList interactionHistory() const;

public slots:
    // This slot is connected to the ViewModel's callback. When the core C++ state changes,
    // this slot is invoked, which in turn emits the `stateChanged` signal to update QML.
    void onStateUpdated();
    
    // --- Methods invokable from QML ---
    // Q_INVOKABLE allows QML to call these C++ methods directly.
    Q_INVOKABLE void selectContact(const QString& peerId);
    Q_INVOKABLE void deselectContact();
    Q_INVOKABLE void addContact(const QString& peerId);
    Q_INVOKABLE void acceptContact(const QString& peerId);

signals:
    // This signal is emitted whenever any part of the UI state changes.
    // QML properties use this to know when they need to re-read their values.
    void stateChanged();

private:
    // Converts the C++ UIState from the ViewModel into a format that QML can understand (QVariantMap).
    void updateState(const eillance::view_model::UIState& newState);

    std::shared_ptr<eillance::view_model::MercuryViewModel> m_viewModel;

    // A mutex to protect access to the state variables from different threads.
    mutable std::mutex m_mutex;

    // --- State variables that cache the data for the QML layer ---
    QString m_myId;
    QVariantList m_contacts;
    QString m_selectedContactId;
    QVariantList m_interactionHistory;
};
