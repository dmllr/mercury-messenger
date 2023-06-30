FROM python:3.9-slim

WORKDIR /a
COPY signaling-server.py ./
COPY ./start.sh ./
COPY ./requirements.txt ./

RUN pip3 install -r requirements.txt

CMD ["bash", "/a/start.sh", "/etc/cert/cert.pem"]
