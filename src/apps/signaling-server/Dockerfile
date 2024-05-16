FROM python:3.9-slim

WORKDIR /a
COPY auth_manager.py ./
COPY signaling_server.py ./
COPY start.sh ./
COPY requirements.txt ./

RUN pip3 install -r requirements.txt

CMD ["bash", "/a/start.sh", "/etc/cert/cert.pem"]
