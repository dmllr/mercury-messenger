# Project Mercury: Build and Deploy

This document provides instructions for building, deploying, and running the various components of Project Mercury.

## 1. Build Environment

Project Mercury relies on a Docker-based development environment to ensure all native components are built with a consistent toolchain.

**Build the Docker Image:**

First, build the development environment image from the `devenv` directory (assuming it exists at the project root):

```shell
docker build -t mercury-devenv devenv
```

## 2. Bootstrap Node

The native C++ applications can be built and deployed using the included scripts in the development environment.

### Build

Compiles `mercury-bootstrap` native target and strips the binary.

```shell
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace mercury-devenv /workspace/tools/deploy-bootstrap/build-bootstrap
```

### Deploy

Copies the binary to the target nodes listed in `devenv/deploy-nodes.txt` and launches it in a tmux session.

```shell
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace mercury-devenv /workspace/tools/deploy-bootstrap/deploy-bootstrap
```

*Optional:* Add `--kill-process` to stop the currently running instance before deploying:

```shell
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace mercury-devenv /workspace/tools/deploy-bootstrap/deploy-bootstrap --kill-process
```

### Build and Deploy (All-in-one)

Builds the project and, if successful, automatically deploys it.

```shell
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace mercury-devenv /workspace/tools/deploy-bootstrap/build-deploy-bootstrap --kill-process
```

**Launch legacy Mercury Server**

```shell
mkdir -p /var/app/signaling-server/certs \
&& sudo cp -r /etc/letsencrypt/archive/app.hela.me /var/app/signaling-server/certs/ \
&& sudo chown -R ubuntu:ubuntu /var/app/signaling-server/certs \
&& FULLCHAIN=$(ls -v /var/app/signaling-server/certs/app.hela.me/fullchain*.pem | tail -n1) \
&& PRIVKEY=$(ls -v /var/app/signaling-server/certs/app.hela.me/privkey*.pem | tail -n1) \
&& /var/app/signaling-server/mercury-server \
    --port=8000 \
    --ssl-cert="$FULLCHAIN" \
    --ssl-cert-key="$PRIVKEY" \
    --log-level=verbose
```

## 3. Android App

### Prerequisites

-   Android SDK
-   Android NDK version `29.0.13113456 rc1` (as specified in `build.gradle`)

### Keystore Setup

To sign the release build, you must create a `keystore.config` file in the `src/apps/android/app/` directory.

1.  Create the file: `src/apps/android/app/keystore.config`
2.  Add the following content, replacing placeholder values with your actual keystore information:

    ```properties
    storeFile=<path_to_your_keystore_file>
    storePassword=<your_keystore_password>
    keyAlias=<your_key_alias>
    keyPassword=<your_key_password>
    ```

### Build the APK

Navigate to the `android` app directory and run the `assembleRelease` Gradle task.

```shell
cd src/apps/android
./gradlew assembleRelease
```

The signed release APK will be generated at: `app/build/outputs/apk/release/app-release.apk`.
