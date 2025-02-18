# Signaling server

Python-based signaling server

## Deployed

/var/app/signaling-server

```bash
rsync -raP ./src/apps/signaling vm0.hela.me:/var/app/
```

### Update
```shell
docker build -t dmllr/main:signalling_v_1_0 .
```

## Exposed

app.hela.me:8000
