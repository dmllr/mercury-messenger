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

The core business logic is encapsulated within a set of C++ libraries collectively known as **Eillance**. End applications (Android, CLI, etc.) are designed to be extremely thin wrappers around this core. They serve as the user interface and platform liaison, handling device-specific concerns (like rendering, input events, and permissions). All state management, cryptography, and network orchestration reside exclusively within the Eillance libraries.

### Component Breakdown

| Library / App | Location | Responsibility |
| :--- | :--- | :--- |
| `eillance-core` | `src/libs/eillance-core` | The primary module containing all application logic, data models, protocol handlers, and the presentation view model that bridges the core state to the UI layer. |
| `eillance-crypto` | `src/libs/eillance-crypto` | A dedicated cryptography layer wrapping BoringSSL for identity management and end-to-end encrypted session establishment. |
| `eillance-rtc` | `src/libs/eillance-rtc` | Provides a cross-platform RTC transport implementation for desktop/CLI clients based on `libdatachannel`. |
| `eillance-storage-sqlite`| `src/libs/eillance-storage-sqlite`| An SQLite-based implementation of the persistent storage interface, handling all database operations and schema migrations. |
| `eillance-utils` | `src/libs/eillance-utils` | A collection of common, reusable utilities such as custom dispatch queues, argument parsers, and data structures. |
| `eillance-logging` | `src/libs/eillance-logging` | A lightweight logging abstraction. |
| `eillance-h264` | `src/libs/eillance-h264` | Helper utilities for parsing H.264 video streams. |
| **Bootstrap Node** | `src/apps/mercury-bootstrap` | A libp2p bootstrap node that provides initial peer discovery and connection relaying across the network. |
| **Android App** | `src/apps/android` | The mobile presentation layer built upon the Eillance libraries, using Google's official WebRTC library for transport. |
| **CLI Client** | `src/apps/mercury-cli` | A minimal application useful for robust end-to-end testing and integration verification, using `libdatachannel` for transport. |

### Tech Stack

#### Key Libraries & Implementations

*   **Cross-platform Core:** C++20
*   **RTC/Data Transport:**
    *   **`libdatachannel`**: Used for the CLI client and future desktop platforms.
    *   **Google WebRTC (`org.webrtc`)**: Used for the Android application.
*   **Database:** SQLite via the `eillance-storage-sqlite` library.
*   **Cryptography:** BoringSSL, abstracted by the `eillance-crypto` library.
*   **CLI Media Handling:**
    *   **OpenCV**: Video capture.
    *   **FFmpeg**: Video encoding.
    *   **miniaudio**: Audio capture.
    *   **Opus**: Audio encoding.
*   **Android UI:** Jetpack Compose.

#### Protocol Stack

Project Mercury utilizes real-time data and media exchange through a Peer Connection, a negotiated peer-to-peer connection capable of carrying both data and media tracks based on the following standards:

**Protocol Stack:**

*   **Data Channels:** SCTP (RFC8831)
*   **Media Transport:** SRTP (RFC8834)
*   **Security & Key Exchange:** DTLS/UDP (RFC7350 and RFC8261)
*   **Connection Establishment:** ICE (RFC8445) with STUN (RFC8489) and its extension TURN (RFC8656)

---

## Build

See the detailed instructions in the [build and deploy](build-and-deploy.md) section.
