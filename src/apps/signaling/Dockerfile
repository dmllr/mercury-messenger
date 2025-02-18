FROM python:3.9-slim

WORKDIR /a

COPY requirements.txt ./
RUN pip3 install -r requirements.txt

COPY auth_manager.py ./
COPY signaling_server.py ./
COPY start.sh ./

CMD ["bash", "/a/start.sh", "/etc/cert/cert.pem"]
