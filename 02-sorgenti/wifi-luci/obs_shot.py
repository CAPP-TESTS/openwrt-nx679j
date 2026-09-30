#!/usr/bin/env python3
"""Cattura uno screenshot del video source di OBS via obs-websocket v5."""
import asyncio, base64, hashlib, json, sys
import websockets

CONFIG = '/home/user/.config/obs-studio/plugin_config/obs-websocket/config.json'
OUT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/phonescreen.png'

async def main():
    cfg = json.load(open(CONFIG))
    pw = cfg['server_password']
    async with websockets.connect('ws://127.0.0.1:4455', max_size=50*1024*1024) as ws:
        hello = json.loads(await ws.recv())
        auth = hello['d'].get('authentication')
        ident = {'op': 1, 'd': {'rpcVersion': 1}}
        if auth:
            secret = base64.b64encode(hashlib.sha256((pw + auth['salt']).encode()).digest()).decode()
            resp = base64.b64encode(hashlib.sha256((secret + auth['challenge']).encode()).digest()).decode()
            ident['d']['authentication'] = resp
        await ws.send(json.dumps(ident))
        r = json.loads(await ws.recv())
        assert r['op'] == 2, f"identify failed: {r}"
        # trova i source
        await ws.send(json.dumps({'op': 6, 'd': {'requestType': 'GetInputList', 'requestId': 'l1'}}))
        rid = 0
        while True:
            m = json.loads(await ws.recv())
            if m.get('op') == 7 and m.get('d', {}).get('requestId') == 'l1':
                inputs = m['d']['responseData']['inputs']
                break
        for i in inputs:
            print(f"  input: {i.get('inputName')} ({i.get('inputKind')})")
        # scegli il video capture
        vc = [i for i in inputs if 'v4l2' in (i.get('inputKind') or '') or 'Video Capture' in (i.get('inputKind') or '')]
        name = vc[0]['inputName'] if vc else inputs[0]['inputName']
        print(f"uso source: {name}")
        req = {'op': 6, 'd': {'requestType': 'SaveSourceScreenshot',
               'requestData': {'sourceName': name, 'imageFormat': 'png',
                               'imageFilePath': OUT, 'imageWidth': 1920, 'imageHeight': 1080,
                               'imageCompressionQuality': 100},
               'requestId': 's1'}}
        await ws.send(json.dumps(req))
        while True:
            m = json.loads(await ws.recv())
            if m.get('op') == 7 and m.get('d', {}).get('requestId') == 's1':
                st = m['d']['requestStatus']
                print(f"status: {st}")
                break
    print(f"OK -> {OUT}")

asyncio.run(main())
