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
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace hela-eillance-devenv bash -c "cmake -GNinja -DCMAKE_BUILD_TYPE=Release -B build-deploy && cmake --build build-deploy --target mercury-server --config Release && strip build-deploy/bin/mercury-server && chmod 400 /workspace/.ssh/vm0.ssh.key && scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i /workspace/.ssh/vm0.ssh.key build-deploy/bin/mercury-server ubuntu@vm0.hela.me:/var/app/signaling-server/"
```

### CMake Configure

```shell
docker run --rm -v ${PWD}:/workspace -w /workspace hela-eillance-devenv bash -c "cmake -GNinja -DCMAKE_BUILD_TYPE=Release -B build-deploy"
```

### Build

```shell
docker run --rm -v ${PWD}:/workspace -w /workspace hela-eillance-devenv bash -c "cmake --build build-deploy --target mercury-server --config Release && strip build-deploy/bin/mercury-server"
```

### Deploy

```shell
docker run --rm -v ${PWD}:/workspace -v ${HOME}/.ssh:/workspace/.ssh -w /workspace hela-eillance-devenv bash -c "chmod  400 /workspace/.ssh/vm0.ssh.key && scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i /workspace/.ssh/vm0.ssh.key build-deploy/bin/mercury-server ubuntu@vm0.hela.me:/var/app/signaling-server/"
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
   FIREBASE_API_KEY=... /var/app/signaling-server/mercury-server --port=8000 --ssl-cert="/etc/letsencrypt/archive/app.hela.me/fullchain18.pem" --ssl-cert-key="/etc/letsencrypt/archive/app.hela.me/privkey18.pem"
   ```
   
   or
   
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
