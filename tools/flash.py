#!/usr/bin/env python3
"""Back up and flash a WeAct G431 in ROM DFU; requires its exact ROM serial.

Disconnect CAN. Hold BOOT0 while reconnecting USB, release, then use dfu-util -l
to obtain the serial. Never writes option bytes. Backup/logs stay in ignored local/.
--check-only validates the release without USB, root or dfu-util.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
from verify_release import ROOT,require,verify

FLASH_BYTES=131072
USER_OFFSET=0x1C000


def select_target(listing,serial):
    matches=[]
    for line in listing.splitlines():
        if '[0483:df11]' not in line:continue
        fields=dict(re.findall(r'(\w+)="([^"]*)"',line))
        if fields.get('serial')!=serial or not re.search(r'\balt=0\b',line):continue
        require(fields.get('name','').startswith('@Internal Flash'),'DFU alt 0 is not internal flash')
        require('/0x08000000/64*02Kg' in fields['name'],'expected G431 128 KiB / 2 KiB flash geometry')
        require(bool(fields.get('path')),'DFU path missing');matches.append(fields['path'])
    require(len(matches)==1,'expected exactly one matching G431 ROM target; check BOOT0/serial with dfu-util -l')
    return matches[0]


def verify_readback(before,after,image):
    require(len(before)==len(after)==FLASH_BYTES,'full flash backup/readback must be 128 KiB')
    require(8<=len(image)<=USER_OFFSET,'candidate size outside application area')
    require(after[:len(image)]==image,'programmed bytes differ from candidate')
    require(after[USER_OFFSET:]==before[USER_OFFSET:],'reserved user pages changed')


def run(argv,path):
    result=subprocess.run(argv,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    path.write_text(result.stdout);print(result.stdout,end='',flush=True)
    require(result.returncode==0,'dfu-util failed; see '+str(path))
    return result.stdout


def save_restore(folder,target,backup):
    require(len(backup)==FLASH_BYTES and (folder/'before.bin').read_bytes()==backup,'invalid saved backup')
    with (folder/'before.bin').open('rb') as stream:os.fsync(stream.fileno())
    sha=hashlib.sha256(backup).hexdigest();quoted=' '.join(shlex.quote(x) for x in target)
    script=folder/'restore-before.sh'
    script.write_text('''#!/usr/bin/env bash
set -euo pipefail
work_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
image="$work_dir/before.bin"
printf '%s  %s\\n' '''+shlex.quote(sha)+''' "$image" | sha256sum -c -
[[ $(wc -c < "$image") -eq 131072 ]]
verify_dir=$(mktemp -d "$work_dir/restore.XXXXXX")
chmod 755 "$verify_dir"
'''+quoted+''' -s 0x08000000 -D "$image"
'''+quoted+''' -s 0x08000000:131072 -U "$verify_dir/readback.bin"
cmp -- "$image" "$verify_dir/readback.bin"
echo "Original full flash restored. Reconnect USB without BOOT0."
''');script.chmod(0o755)
    return script


def program(executable,serial,profile,folder):
    listing=run([executable,'-l'],folder/'devices.log');port=select_target(listing,serial)
    target=[executable,'-d','0483:df11','-S',serial,'-p',port,'-a','0']
    run(target+['-s','0x08000000:131072','-U',str(folder/'before.bin')],folder/'backup.log')
    before=(folder/'before.bin').read_bytes();require(len(before)==FLASH_BYTES,'short backup; no programming performed')
    restore=save_restore(folder,target,before)
    image=Path(profile['image']).read_bytes()
    require(hashlib.sha256(image).hexdigest()==profile['image_sha256'],'candidate changed before programming')
    print('Full backup and restore script saved:',folder,flush=True)
    run(target+['-s','0x08000000','-D',profile['image']],folder/'download.log')
    run(target+['-s','0x08000000:131072','-U',str(folder/'after.bin')],folder/'readback.log')
    after=(folder/'after.bin').read_bytes();verify_readback(before,after,image)
    return {'result':'pass','usb_path':port,'candidate':profile,'backup_sha256':hashlib.sha256(before).hexdigest(),
            'readback_sha256':hashlib.sha256(after).hexdigest(),'all_user_pages_preserved':True,'restore_script':str(restore)}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--serial',help='exact serial from dfu-util -l (required for programming)');parser.add_argument('--check-only',action='store_true');args=parser.parse_args()
    try:profile=verify()
    except (OSError,ValueError,KeyError) as error:parser.error(str(error))
    print('Verified release:',json.dumps(profile),flush=True)
    if args.check_only:return 0
    if not args.serial:parser.error('--serial is required; list ROM targets using dfu-util -l')
    executable=shutil.which('dfu-util')
    if not executable:parser.error('dfu-util is required')
    os.umask(0o022);local=ROOT/'local';local.mkdir(exist_ok=True)
    folder=Path(tempfile.mkdtemp(prefix='flash.',dir=local));folder.chmod(0o755)
    try:
        result=program(executable,args.serial,profile,folder)
        (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print('PASS: candidate bytes and all reserved user pages verified.\nUnplug/reconnect USB WITHOUT BOOT0. Logs/backup: '+str(folder),flush=True);return 0
    except (Exception,KeyboardInterrupt) as error:
        (folder/'result.json').write_text(json.dumps({'result':'fail','error':str(error)},indent=2)+'\n')
        print('FAIL:',str(error),'\nLogs:',folder,flush=True)
        if (folder/'restore-before.sh').exists():print('Optional recovery: enter ROM with BOOT0, then run sudo bash '+shlex.quote(str(folder/'restore-before.sh')),flush=True)
        return 1


if __name__=='__main__':raise SystemExit(main())
