#!/usr/bin/env python3
"""Owner-authorized LAN delivery of one verified native-test CIA only."""
import argparse
import hashlib
import ipaddress
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from verify_integrated_cia import verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=int)
    parser.add_argument('--port', type=int, default=8787)
    args = parser.parse_args()
    if args.build < 1 or not 1024 <= args.port <= 65535:
        parser.error('Invalid build or unprivileged TCP port')
    package = ROOT / 'build/native-renderer/playable-r1/hardware' / f'build{args.build}'
    cia = package / f'SM64DS-Native-BUILD{args.build}.cia'
    elf = package / 'sm64ds.elf'
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    checked = verify(cia, elf, sha(elf), native_test=True)
    address = subprocess.check_output(['ipconfig', 'getifaddr', 'en0'], text=True).strip()
    ip = ipaddress.ip_address(address)
    if ip.version != 4 or not ip.is_private or ip.is_loopback:
        raise ValueError('A private IPv4 LAN address is required')
    with socket.socket() as probe:
        probe.bind(('0.0.0.0', args.port))
    delivery = package / 'lan-delivery'
    delivery.mkdir(exist_ok=False)
    served = delivery / 'serve'
    token = checked['sha256'][:16]
    destination = served / token / cia.name
    destination.parent.mkdir(parents=True)
    shutil.copyfile(cia, destination)
    if sha(destination) != checked['sha256']:
        raise ValueError('CIA copy failed integrity verification')
    url = f'http://{address}:{args.port}/{token}/{cia.name}'
    import qrcode
    qr = delivery / 'FBI-QR.png'
    qrcode.make(url, box_size=12, border=4).save(qr)
    decoder = delivery / 'decode.swift'
    decoder.write_text('''import Foundation
import Vision
let request = VNDetectBarcodesRequest()
request.symbologies = [.qr]
let handler = VNImageRequestHandler(url: URL(fileURLWithPath: CommandLine.arguments[1]), options: [:])
try handler.perform([request])
let data = try JSONSerialization.data(withJSONObject: (request.results ?? []).compactMap { $0.payloadStringValue }, options: [.sortedKeys])
print(String(data: data, encoding: .utf8)!)
''')
    decoded = json.loads(subprocess.check_output(['swift', str(decoder), str(qr)], text=True))
    if decoded != [url]:
        raise ValueError('The QR does not decode to the exact CIA URL')
    with (delivery / 'server.log').open('w') as log:
        # HTTP/1.1, no Nagle, large writes: FBI timed out on the stock server.
        server = subprocess.Popen([sys.executable, str(ROOT / 'tools/native_renderer/lan_server.py'),
                                   str(args.port), str(served)],
                                  stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                  start_new_session=True)
    try:
        for attempt in range(30):
            if server.poll() is not None:
                raise RuntimeError('The owned delivery server exited')
            try:
                with urllib.request.urlopen(url, timeout=10) as response:
                    payload = response.read()
                    if response.status != 200 or hashlib.sha256(payload).hexdigest() != checked['sha256']:
                        raise ValueError('LAN download does not match the verified CIA')
                break
            except urllib.error.URLError:
                if attempt == 29:
                    raise
                time.sleep(.1)
        result = dict(build=args.build, url=url, qr=str(qr), cia=str(cia),
                      cia_sha256=checked['sha256'], cia_bytes=checked['bytes'],
                      decoded_payloads=decoded, server_pid=server.pid,
                      server_root=str(served), downloaded_sha256=hashlib.sha256(payload).hexdigest(),
                      physical_installation_verified=False,
                      limits='Same LAN, awake Mac and unchanged IP required; no public upload.')
        (delivery / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
    except BaseException:
        server.terminate()
        server.wait(timeout=10)
        raise


if __name__ == '__main__':
    main()
