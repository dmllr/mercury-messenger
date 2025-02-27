FROM python:3.9-slim

WORKDIR /a

COPY requirements.txt ./
RUN pip3 install -r requirements.txt

COPY signaling_server.py ./

CMD ["bash", "/a/start.sh", "/etc/cert/cert.pem"]
