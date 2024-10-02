# Configure
## Configure `~/.ssh/config`
```text
Host vm0.hela.me
HostName vm0.hela.me
IdentityFile ~/.ssh/ssh-key-2022-03-08.key
User ubuntu
```

# Build
## Prerequisites
### Ubuntu
```sh
sudo apt install liblzma-dev
sudo apt install libiconv-hook-dev
```

### MacOS


### Windows
Install MSYS2 from https://github.com/msys2/msys2-installer/releases/download/2024-01-13/msys2-x86_64-20240113.exe, then

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-x86_64-zlib
pacman -S mingw-w64-ucrt-x86_64-diffutils
pacman -S cmake
pacman -S ninja
pacman -S make
```

# Run
1. Check app.hela.me:8000
   
   If any problem, restart the signaling server
   ```shell
   ssh -i ~/Downloads/ssh-key-2022-03-08.key ubuntu@app.hela.me
   ```
   ```shell
   docker run \
     -d \
     --rm \
     --name signaling \
     -p 8000:8000 \
     -v /etc/letsencrypt:/etc/letsencrypt \
     dmllr/main:signaling_v_1_0 \
     bash -c "python3 signaling_server.py 0.0.0.0:8000 /etc/letsencrypt/live/app.hela.me/fullchain.pem /etc/letsencrypt/live/app.hela.me/privkey.pem"
   ```
2. Browse <a href="https://app.hela.me/camera/web-client">https://app.hela.me/camera/web-client</a>
3. Push "Start"
4. Run app
