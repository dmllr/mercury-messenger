docker run \
  -d \
  --rm \
  --name signalling \
  --network host \
  -v /etc/letsencrypt/archive/app.hela.me/:/etc/cert \
  dmllr/main:signalling_v_1_0 \
  python3 signaling_server.py 0.0.0.0:8000 /etc/cert/fullchain16.pem /etc/cert/privkey16.pem
