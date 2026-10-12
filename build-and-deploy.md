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

The native C++ applications can be built and deployed using the following commands.

### Oneliner (Build & Deploy Bootstrap Node)

This single command configures the project, builds the `mercury-bootstrap`, strips the binary for size, and deploys it to the target machine via `scp`.

```shell
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace mercury-devenv bash -c "git config --global --add safe.directory /workspace && cmake -GNinja -DCMAKE_BUILD_TYPE=Release -B build-deploy && cmake --build build-deploy --target mercury-bootstrap --config Release && strip build-deploy/src/apps/mercury-bootstrap/mercury-bootstrap && chmod 400 /workspace/.ssh/vm0.ssh.key && scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i /workspace/.ssh/vm0.ssh.key build-deploy/src/apps/mercury-bootstrap/mercury-bootstrap ubuntu@vm0.hela.me:/var/app/mercury-bootstrap/"
```

### Manual Build Steps

#### 1. CMake Configure

This step generates the build files inside a `build-deploy` directory. It only needs to be run once unless you change the `CMakeLists.txt` files.

```shell
docker run --rm -v ${PWD}:/workspace -w /workspace mercury-devenv bash -c "git config --global --add safe.directory /workspace && cmake -GNinja -DCMAKE_BUILD_TYPE=Release -B build-deploy"
```

#### 2. Build

This command compiles `mercury-bootstrap` native target.

```shell
docker run --rm -v ${PWD}:/workspace -w /workspace mercury-devenv bash -c "cmake --build build-deploy --target mercury-bootstrap --config Release"
```

The compiled binary will be located at `build-deploy/src/apps/mercury-bootstrap/mercury-bootstrap`.

#### 3. Deploy

Use `scp` to copy the `mercury-bootstrap` binary to your deployment machine. This example requires you to have SSH keys configured.

```shell
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace mercury-devenv bash -c 'chmod 400 /workspace/.ssh/vm0.ssh.key && SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i /workspace/.ssh/vm0.ssh.key" && SERVERS=(jp-bn-101 jp-bn-102 us-bn-101 us-bn-102); for s in "${SERVERS[@]}"; do echo "Deploying to ${s}.eillance.com..."; ssh $SSH_OPTS ubuntu@${s}.eillance.com "sudo mkdir -p /var/app/mercury-bootstrap/; sudo chown -R ubuntu:ubuntu /var/app"; scp $SSH_OPTS build-deploy/src/apps/mercury-bootstrap/mercury-bootstrap ubuntu@${s}.eillance.com:/var/app/mercury-bootstrap/; done'
```

### Running the Bootstrap Node

Connect to your server via SSH and run the executable.

**SSH Configuration (`~/.ssh/config`)**

For convenience, you can configure an SSH alias on your local machine:

```text
Host vm0.hela.me
  HostName vm0.hela.me
  IdentityFile ~/.ssh/vm0.ssh.key
  User ubuntu
```

**Launch a bootstrap node**

```shell
mkdir -p /var/app/mercury-bootstrap \
&& /var/app/mercury-bootstrap/mercury-bootstrap \
    --port=807 \
    --log-level=verbose
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
