"""Offline checks for release validation and full-backup DFU safeguards."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import flash
import verify_release as verify

SERIAL='TEST-G431'
LISTING='Found DFU: [0483:df11] ver=0200, path="3-7", alt=0, name="@Internal Flash   /0x08000000/64*02Kg", serial="TEST-G431"\n'


def record(address,kind,payload=b''):
    data=bytes([len(payload)])+address.to_bytes(2,'big')+bytes([kind])+payload
    return ':'+(data+bytes([-sum(data)%256])).hex()+'\n'


class ReleaseChecks(unittest.TestCase):
    def test_hex_matches_bytes_and_rejects_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'firmware.hex'
            valid=record(0,4,b'\x08\0')+record(0,0,b'abcd')+record(0,1)
            path.write_text(valid);self.assertEqual(verify.hex_image(path,0x08000000,4),b'abcd')
            for text in (valid.replace('61626364','61626365'),valid+record(0,0,b'a'),record(0,4,b'\x08\0')+record(0,0,b'abcd')*2+record(0,1)):
                path.write_text(text)
                with self.assertRaises(ValueError):verify.hex_image(path,0x08000000,4)

    def test_manifest_rejects_changes_empty_and_escape(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);file=root/'data';file.write_bytes(b'known');manifest=root/'hashes'
            manifest.write_text(hashlib.sha256(b'known').hexdigest()+'  data\n')
            self.assertEqual(verify.check_manifest(manifest,root),1)
            file.write_bytes(b'changed')
            with self.assertRaises(ValueError):verify.check_manifest(manifest,root)
            for value in ('','0'*64+'  ../outside\n'):
                manifest.write_text(value)
                with self.assertRaises(ValueError):verify.check_manifest(manifest,root)

    def test_bundled_release_verifies(self):
        self.assertEqual(verify.verify()['version'],'0x26111501')


class DfuGuards(unittest.TestCase):
    def test_target_requires_exact_serial_capacity_and_unique_path(self):
        self.assertEqual(flash.select_target(LISTING,SERIAL),'3-7')
        for listing in ('',LISTING.replace(SERIAL,'OTHER'),LISTING.replace('64*02Kg','128*02Kg'),LISTING*2):
            with self.assertRaises(ValueError):flash.select_target(listing,SERIAL)

    def test_readback_requires_image_and_every_reserved_byte(self):
        before=bytes(131072);image=b'firmware';after=image+before[len(image):]
        flash.verify_readback(before,after,image)
        for offset in (0,0x1C000,131071):
            bad=bytearray(after);bad[offset]^=1
            with self.assertRaises(ValueError):flash.verify_readback(before,bytes(bad),image)
        with self.assertRaises(ValueError):flash.verify_readback(before,after[:-1],image)

    def program(self,folder,short=False,corrupt=False):
        before=bytes([0xFF])*131072;image=b'candidate';path=folder/'candidate.bin';path.write_bytes(image)
        profile={'image':str(path),'image_sha256':hashlib.sha256(image).hexdigest()};events=[]
        def run(argv,log):
            events.append(argv)
            if '-l' in argv:return LISTING
            if '-D' in argv:
                self.assertTrue((folder/'restore-before.sh').is_file())
                self.assertEqual((folder/'before.bin').read_bytes(),before)
                return ''
            if '-U' in argv:
                output=Path(argv[-1])
                if output.name=='before.bin':output.write_bytes(before[:-1] if short else before)
                else:
                    after=bytearray(image+before[len(image):])
                    if corrupt:after[0x1C000]^=1
                    output.write_bytes(after)
            return ''
        return profile,events,run

    def test_full_backup_precedes_programming_and_restore_is_guarded(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);profile,events,run=self.program(folder)
            with patch.object(flash,'run',run):result=flash.program('dfu-util',SERIAL,profile,folder)
            self.assertEqual(result['result'],'pass');self.assertTrue(result['all_user_pages_preserved'])
            self.assertEqual([('-D' in a,'-U' in a) for a in events],[(False,False),(False,True),(True,False),(False,True)])
            script=folder/'restore-before.sh';subprocess.run(['bash','-n',str(script)],check=True)
            self.assertIn(result['backup_sha256'],script.read_text());self.assertIn('cmp --',script.read_text())
            self.assertTrue(all('--unprotect' not in a and ':leave' not in ' '.join(a) for a in events))

    def test_short_backup_never_programs(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);profile,events,run=self.program(folder,short=True)
            with patch.object(flash,'run',run),self.assertRaises(ValueError):flash.program('dfu-util',SERIAL,profile,folder)
            self.assertFalse(any('-D' in a for a in events))

    def test_changed_user_page_is_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);profile,events,run=self.program(folder,corrupt=True)
            with patch.object(flash,'run',run),self.assertRaises(ValueError):flash.program('dfu-util',SERIAL,profile,folder)


if __name__=='__main__':unittest.main()
