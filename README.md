# Build
## Prerequisites
sudo apt install liblzma-dev
sudo apt install libiconv-hook-dev

# Run
1. Check app.hela.me:8000
   
   If any problem, restart the signalling server
   ```shell
   ssh -i ~/Downloads/ssh-key-2022-03-08.key ubuntu@app.hela.me
   ```
   ```shell
   docker run \
     -d \
     --rm \
     --name signalling \
     -p 8000:8000 \
     -v /etc/letsencrypt:/etc/letsencrypt \
     dmllr/main:signalling_v_1_0 \
     bash -c "python3 signaling-server.py 0.0.0.0:8000 /etc/letsencrypt/live/app.hela.me/fullchain.pem /etc/letsencrypt/live/app.hela.me/privkey.pem"
   ```
2. Browse <a href="https://app.hela.me/camera/web-client">https://app.hela.me/camera/web-client </a>
3. Push "Start"
4. Run app
