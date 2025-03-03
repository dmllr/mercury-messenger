# Eillance

## Prerequisites

### Build the Development Environment

```shell
docker build -t hela-eillance-devenv devenv
```

Development Environment used to compile and deploy the Signaling server.

## Signaling Server

### Oneliner

```shell
docker run --rm -v ${HOME}:${HOME} -w ${PWD} \
  hela-eillance-devenv bash -c \
  "cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=src/cmake/toolchain-x86_64-clang.cmake -B build-deploy && cmake --build build-deploy --target hela-eillance-signaling --config Release"
```

### CMake Configure

```shell
docker run --rm -v ${HOME}:${HOME} -w ${PWD} hela-eillance-devenv cmake -GNinja -DCMAKE_BUILD_TYPE=Release -B build-deploy
```

### Build

```shell
docker run --rm -v ${HOME}:${HOME} -w ${PWD} hela-eillance-devenv cmake --build build-deploy --target hela-eillance-signaling --config Release
```

### Server-side configuration

#### Configure `~/.ssh/config`

```text
Host vm0.hela.me
HostName vm0.hela.me
IdentityFile ~/.ssh/ssh-key-2022-03-08.key
User ubuntu
```

## Run

1. Check app.hela.me:8000
   
   If any problem, restart the signaling server
   ```shell
   ssh -i ~/Downloads/ssh-key-2022-03-08.key ubuntu@app.hela.me
   ```
   ```shell
   docker run \
     -it \
     --rm \
     --name signaling \
     --network host \
     -v /etc/letsencrypt/archive/app.hela.me/:/etc/cert \
     -v /var/secret:/etc/secret \
     dmllr/main:signalling_v_1_0 \
     python3 signaling_server.py 0.0.0.0:8000 /etc/cert/fullchain18.pem /etc/cert/privkey18.pem
   ```
2. Browse <a href="https://app.hela.me/camera/web-client">https://app.hela.me/camera/web-client</a>
3. Push "Start"
4. Run app
