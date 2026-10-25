#!/usr/bin/env python3
"""Serve only a verified local CIA/3DSX package and decode its FBI QR."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--port',type=int,default=8787)
    a=p.parse_args();assert 1024<=a.port<=65535
    from verify_integrated_cia import verify
    package=a.package.resolve();candidate=a.candidate.resolve()
    build=json.loads((candidate/'BUILD.json').read_text());prefix=build['ELF_sha256'][:16]
    cia=package/('sm64ds-native-'+prefix+'.cia')
    checked=verify(cia,candidate/'sm64ds-integrated.elf',build['ELF_sha256'])
    assert build['rom_only'] and sha(package/'SD/3ds/sm64ds/sm64ds.3dsx')==build['image_sha256']
    address=subprocess.check_output(['ipconfig','getifaddr','en0'],text=True).strip()
    ip=ipaddress.ip_address(address);assert ip.version==4 and ip.is_private and not ip.is_loopback
    with socket.socket() as probe:probe.bind(('0.0.0.0',a.port))
    folder=ROOT/'build/integrated-native'/('delivery-'+prefix+'-'+str(time.time_ns()))
    served=folder/'serve';served.mkdir(parents=True)
    allow={'SM64DS-ROM-Only.cia':cia,'sm64ds.3dsx':package/'SD/3ds/sm64ds/sm64ds.3dsx'}
    for name in ['LEEME.txt','CREDITS.txt','LICENSE-vendor.txt','LICENSE-DSP1.txt']:allow[name]=package/name
    for name,source in allow.items():shutil.copyfile(source,served/name)
    with zipfile.ZipFile(served/'SM64DS-ROM-Only.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for name in allow:archive.write(served/name,'SD/3ds/sm64ds/sm64ds.3dsx' if name=='sm64ds.3dsx' else name)
    (served/'SHA256SUMS.txt').write_text(''.join(sha(f)+'  '+f.name+'\n' for f in sorted(served.iterdir())))
    url=f'http://{address}:{a.port}/SM64DS-ROM-Only.cia'
    import qrcode
    qr=folder/'FBI-QR.png';qrcode.make(url,box_size=12,border=4).save(qr)
    decoder=folder/'decode.swift'
    decoder.write_text('''import Foundation
import Vision
let request = VNDetectBarcodesRequest()
request.symbologies = [.qr]
let handler = VNImageRequestHandler(url: URL(fileURLWithPath: CommandLine.arguments[1]), options: [:])
try handler.perform([request])
let data = try JSONSerialization.data(withJSONObject: (request.results ?? []).compactMap { $0.payloadStringValue }, options: [.sortedKeys])
print(String(data: data, encoding: .utf8)!)
''')
    decoded=json.loads(subprocess.check_output(['swift',str(decoder),str(qr)],text=True))
    assert decoded==[url], 'QR did not decode to the exact CIA URL'
    with (folder/'server.log').open('w') as log:
        process=subprocess.Popen([sys.executable,'-m','http.server',str(a.port),'--bind','0.0.0.0',
            '--directory',str(served)],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,
            start_new_session=True)
    try:
        fetched=None
        for _ in range(30):
            assert process.poll() is None,'Owned server failed to start'
            try:
                with urllib.request.urlopen(url,timeout=5) as response:
                    assert response.status==200;fetched=response.read()
                break
            except (OSError,urllib.error.URLError):time.sleep(.1)
        assert fetched is not None and hashlib.sha256(fetched).hexdigest()==checked['sha256']
        result=dict(status='local_QR_decoded_and_served_CIA_verified',ELF_sha256=build['ELF_sha256'],
            cia_sha256=checked['sha256'],cia_bytes=checked['bytes'],url=url,qr=str(qr),
            decoded_payloads=decoded,server_pid=process.pid,server_root=str(served),
            exact_CIA=str(cia),image_sha256=build['image_sha256'],title_id=checked['title_id'],
            served_files={f.name:sha(f) for f in served.iterdir()},physical_installation=False,
            limits='LAN transfer proof only. Requires same Wi-Fi, unchanged address and awake Mac; physical installation and Old/New FPS are separate.')
        (folder/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))
    except BaseException:
        if process.poll() is None:process.terminate();process.wait(timeout=10)
        raise

if __name__=='__main__':main()
