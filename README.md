# Project Mercury

## About

Project Mercury is a private and secure cross-platform app utilizing peer-to-peer encrypted messaging and voice/video calls.

It operates without traditional registration; users are identified solely via an anonymous ID number. Text messages, voice, and video calls are fully encrypted and transferred directly between devices (P2P).

**Key Features:**

*   **Registration-Free:** Users are identified solely by an anonymous, non-linked ID number.
*   **P2P Encryption:** Text messages, voice, and video calls are fully encrypted and transferred directly device-to-device (P2P).

---

## Architecture

### Architectural Philosophy

Project Mercury is built around a strict separation of concerns where the **Core** module acts as the sole engine of the application.

End applications (Android, CLI, etc.) are designed to be extremely thin wrappers - they are the user interface and platform liaison, handling device-specific concerns (like rendering, input events, and permissions). All core business logic, state management, cryptography, and network orchestration must reside exclusively within the Core module.

### Component Breakdown

| Component | Location | Responsibility |
| :--- | :--- | :--- |
| **Core Engine** | `src/core` | The primary module containing all application logic, data models, networking protocols, encryption handlers, and presentation view models. |
| **Signaling Server** | `src/server` | Handles initial discovery, serving as an intermediary to help peers find each other and establish a direct connection while minimizing exposure of private metadata. |
| **Android App** | `src/apps/android` | The mobile presentation layer built upon the Core Engine. |
| **CLI Client** | `src/apps/mercury-cli` | A minimal application useful for robust end-to-end testing and integration verification. |

### Tech Stack

Project Mercury utilizes real-time data and media exchange through a Peer Connection, a signaled peer-to-peer connection capable of carrying both data and media tracks.

**Protocol Stack:**

*   **Data Channels:** SCTP (RFC8831)
*   **Media Transport:** SRTP (RFC8834)
*   **Security & Key Exchange:** DTLS/UDP (RFC7350 and RFC8261)
*   **Connection Establishment:** ICE (RFC8445) with STUN (RFC8489) and its extension TURN (RFC8656)

---

## Build

See the detailed instructions in the [build and deploy](build-and-deploy.md) section.
