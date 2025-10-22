# Project Mercury

## About this app

Private and secure cross-platform app with peer-to-peer encrypted messaging and voice/video calls.

Works without registration, users are identified with an ID number that is anonymous and not linked to any identity. Text messages, voice and video calls are encrypted and transferred directly device to device (P2P).

### High-level Architecture

#### Directory Structure

 - [src/core](src/core)
   The Core module. Contains all the logic, data and view models for user-facing applications. End applications are designed to be only UI-wrappers for Core.  
 - [src/server](src/server)
   Signaling server. It is responsible to serve as an intermediary to let two peers find and establish a direct connection while minimizing exposure of private information as much as possible. 
 - [src/apps/android](src/apps/android)
   Android application.
 - [src/apps/mercury-cli](src/apps/mercury-cli)
   CLI application useful for testing

### Tech stack

Utilizes real-time data and media exchange between two devices through a Peer Connection, a signaled peer-to-peer connection which can carry both data and media tracks.

Protocol stack:

 * SCTP-based Data Channels (RFC8831)
 * SRTP-based Media Transport (RFC8834)
 * DTLS/UDP (RFC7350 and RFC8261)
 * ICE (RFC8445) with STUN (RFC8489) and its extension TURN (RFC8656)

## Build

See the [build and deploy](build-and-deploy.md) section.
