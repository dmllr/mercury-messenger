docker run \
  -d \
  --rm \
  --name signaling \
  -p 8000:8000 \
  -v /etc/letsencrypt/archive/app.hela.me/:/etc/cert \
  dmllr/main:signaling_v_1_0 \
  python3 signaling-server.py 0.0.0.0:8000 /etc/cert/fullchain1.pem /etc/cert/privkey1.pem
