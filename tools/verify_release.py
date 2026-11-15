#!/usr/bin/env python3
"""Verify the bundled firmware, Intel HEX contents and frozen source inputs."""
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]


def require(ok,message):
    if not ok:raise ValueError(message)


def checksum(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def check_manifest(path,root):
    root=root.resolve();count=0
    for line in path.read_text().splitlines():
        if not line.strip():continue
        digest,name=line.split(maxsplit=1);file=(root/name).resolve()
        require(file.is_relative_to(root),'manifest escapes repository')
        require(file.is_file() and checksum(file)==digest,'checksum mismatch: '+name)
        count+=1
    require(count>0,'empty manifest');return count


def hex_image(path,base,length):
    data=bytearray(length);high=0;seen=set();ended=False
    for line in path.read_text().splitlines():
        require(line.startswith(':') and not ended,'invalid HEX record or data after EOF')
        row=bytes.fromhex(line[1:]);require(len(row)>=5 and len(row)==row[0]+5 and sum(row)%256==0,'HEX length/checksum mismatch')
        count=row[0];address=int.from_bytes(row[1:3],'big');kind=row[3];payload=row[4:-1]
        if kind==0:
            offset=high+address-base;require(0<=offset and offset+count<=length,'HEX outside firmware image')
            for i,value in enumerate(payload,offset):
                require(i not in seen,'overlapping HEX data');seen.add(i);data[i]=value
        elif kind==1:require(count==0 and address==0,'invalid HEX EOF');ended=True
        elif kind==4:require(count==2 and address==0,'invalid HEX linear address');high=int.from_bytes(payload,'big')<<16
        elif kind==2:require(count==2 and address==0,'invalid HEX segment address');high=int.from_bytes(payload,'big')<<4
        elif kind==5:require(count==4 and address==0,'invalid HEX entry record')
        else:raise ValueError('unsupported HEX record')
    require(ended and 0 in seen and length-1 in seen,'incomplete HEX image')
    return bytes(data)


def verify(root=ROOT):
    root=Path(root);meta=json.loads((root/'dist/release.json').read_text());folder=root/'dist'/meta['version']
    for entry in meta['files'].values():
        path=(folder/entry['name']).resolve()
        require(path.parent==folder.resolve(),'artifact escapes release folder')
        require(path.is_file() and path.stat().st_size==entry['bytes'] and checksum(path)==entry['sha256'],'release checksum mismatch: '+path.name)
    image=folder/meta['files']['bin']['name'];raw=image.read_bytes();base=int(meta['flash_base'],16)
    require(8<=len(raw)<=int(meta['user_flash_offset'],16),'image overlaps reserved user pages')
    stack,entry=struct.unpack_from('<II',raw)
    require(0x20000000<stack<=0x20008000 and stack%8==0 and entry&1 and base<=entry&~1<base+len(raw),'invalid G431 application vectors')
    require(hex_image(folder/meta['files']['hex']['name'],base,len(raw))==raw,'HEX and BIN disagree')
    count=check_manifest(root/'verification/SOURCE_MANIFEST.sha256',root)
    return {'version':meta['version'],'image':str(image),'image_bytes':len(raw),'image_sha256':meta['files']['bin']['sha256'],'source_inputs_checked':count}


def main():
    try:print(json.dumps(verify(),indent=2))
    except (OSError,ValueError,KeyError) as error:raise SystemExit(str(error))


if __name__=='__main__':main()
