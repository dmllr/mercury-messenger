# Eillance Web Viewer

A web-based client to view streams from Eillance streamer devices using WebRTC.

## Features

*   Firebase Authentication (Email/Password, Google, Phone) via FirebaseUI
*   Connects to the Eillance signaling server
*   Lists available streamer devices
*   Establishes WebRTC connection to view video/audio streams
*   Allows changing remote camera/resolution via data channel
*   Supports Dark/Light themes based on system preference
*   Basic status indicators

## Setup

1.  **Clone the repository:**
    ```bash
    git clone <your-repo-url>
    cd eillance-web-viewer
    ```
2.  **Install dependencies:**
    ```bash
    npm install
    # or
    yarn install
    ```
3.  **Configure Firebase:**
    *   Create a Firebase project at [https://console.firebase.google.com/](https://console.firebase.google.com/).
    *   Enable Authentication providers (Email/Password, Google, Phone Number).
    *   In your Firebase project settings, go to "Project settings" > "General" and find your web app configuration (or add a web app if you haven't).
    *   Copy the configuration object.
    *   Create a file `src/services/firebaseConfig.ts`.
    *   Paste the configuration into `src/services/firebaseConfig.ts` like this:
        ```typescript
        // src/services/firebaseConfig.ts
        export const firebaseConfig = {
          apiKey: "YOUR_API_KEY",
          authDomain: "YOUR_AUTH_DOMAIN",
          projectId: "YOUR_PROJECT_ID",
          storageBucket: "YOUR_STORAGE_BUCKET",
          messagingSenderId: "YOUR_MESSAGING_SENDER_ID",
          appId: "YOUR_APP_ID"
        };
        ```
    *   **IMPORTANT:** Create a copy named `src/services/firebaseConfig.ts.example` and remove the sensitive values. `firebaseConfig.ts` is already in `.gitignore`.
        ```typescript
        // src/services/firebaseConfig.ts.example
        export const firebaseConfig = {
          apiKey: "YOUR_API_KEY", // Replace with your actual API key in firebaseConfig.ts
          authDomain: "YOUR_AUTH_DOMAIN", // Replace with your actual auth domain in firebaseConfig.ts
          // ... other config placeholders
        };
        ```
4.  **Run the development server:**
    ```bash
    npm run dev
    # or
    yarn dev
    ```
5.  Open your browser to the URL provided (usually `http://localhost:5173`).

## Build for Production

```bash
npm run build
# or
yarn build
