#!/usr/bin/env python3
"""Offline, no-overwrite preparation of explicitly supplied AOD dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import zipfile
import zlib

LICENSE_NAMES=('MODEL_LICENSE.txt','ORT_LICENSE.txt','ORT_THIRD_PARTY_NOTICES.txt')
ARM64_MEMBER='jni/arm64-v8a/libonnxruntime.so'
MODEL_URL='https://huggingface.co/h143N/birefnet-lite-512-chunked/resolve/d0b0c299a1d10b0a57b04d0f1f6088d5c0d6ebf2/onnx/model.onnx'
AAR_URL='https://repo.maven.apache.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.31.0/onnxruntime-android-1.31.0.aar'
LICENSE_SHA256={
    'MODEL_LICENSE.txt':'b5cd5faac84a0acb8a6bdb8aeef0919e528891be27216df84eab398d7c8bd2aa',
    'ORT_LICENSE.txt':'2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c',
    'ORT_THIRD_PARTY_NOTICES.txt':'143764b952fdb1a7c69ce653bfba74a7744d6a8a573bfb73e235fba356c83de3',
}

class PreparationError(Exception): pass

class Spec:
    def __init__(self, model_size, model_sha256, aar_size, aar_sha256, so_size, so_sha256):
        self.model_size=model_size; self.model_sha256=model_sha256
        self.aar_size=aar_size; self.aar_sha256=aar_sha256
        self.so_size=so_size; self.so_sha256=so_sha256

PINNED=Spec(193268476,'16f6a4f726ff851eb6b50b56daf54c426c836a7372f44df21b2065af2d915d23',
            55138854,'72683787a80d0608a3121b967d8e1239aef48286826661c953211626690540a7',
            34208680,'8005a741e57117b4d7b476417706c0e9af13cf1b89db47e60f0cbc56e1af91e0')

def _copy_verified(source,destination,expected_size,expected_sha,label):
    digest=hashlib.sha256(); size=0
    with destination.open('xb') as target:
        while True:
            block=source.read(1024*1024)
            if not block: break
            size+=len(block)
            if size>expected_size: raise PreparationError(label+' exceeds expected size')
            digest.update(block); target.write(block)
        target.flush(); os.fsync(target.fileno())
    if size!=expected_size or digest.hexdigest()!=expected_sha:
        raise PreparationError(label+' size or SHA-256 mismatch')

def _same_object(path,identity):
    try:
        st=os.stat(path,follow_symlinks=False)
        return stat.S_ISREG(st.st_mode) and (st.st_dev,st.st_ino)==identity
    except FileNotFoundError:
        return False

def prepare_assets(model,aar,out,spec,licenses,expected_licenses):
    model=Path(model); aar=Path(aar); out=Path(out); licenses=Path(licenses)
    if os.path.lexists(out): raise PreparationError('output path already exists')
    parent=out.parent.resolve(strict=True)
    if not parent.is_dir(): raise PreparationError('output parent is not a directory')
    out=parent/out.name
    published=[]; created=False; directory_identity=None
    try:
        with tempfile.TemporaryDirectory(prefix='.rme-aod-stage-',dir=parent) as work_name:
            work=Path(work_name)
            with model.open('rb') as source:
                _copy_verified(source,work/'model.onnx',spec.model_size,spec.model_sha256,'model')
            with aar.open('rb') as source:
                _copy_verified(source,work/'input.aar',spec.aar_size,spec.aar_sha256,'AAR')
            with zipfile.ZipFile(work/'input.aar','r') as archive:
                matches=[x for x in archive.infolist() if x.filename==ARM64_MEMBER]
                if len(matches)!=1 or matches[0].is_dir() or matches[0].file_size!=spec.so_size:
                    raise PreparationError('AAR ARM64 library missing, duplicated, or wrong size')
                with archive.open(matches[0],'r') as source:
                    _copy_verified(source,work/'libonnxruntime.so',spec.so_size,spec.so_sha256,'ARM64 library')
            license_digests={}
            for name in LICENSE_NAMES:
                data=(licenses/name).read_bytes()
                if not data or len(data)>1024*1024: raise PreparationError('invalid license file: '+name)
                license_digests[name]=hashlib.sha256(data).hexdigest()
                if license_digests[name]!=expected_licenses.get(name):
                    raise PreparationError('license SHA-256 mismatch: '+name)
                (work/name).write_bytes(data)
            manifest={
                'format':1,
                'model':{'source':MODEL_URL,'size':spec.model_size,'sha256':spec.model_sha256},
                'runtime_aar':{'source':AAR_URL,'size':spec.aar_size,'sha256':spec.aar_sha256},
                'runtime_arm64':{'member':ARM64_MEMBER,'size':spec.so_size,'sha256':spec.so_sha256},
                'licenses':license_digests,
            }
            (work/'SOURCES.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n',encoding='utf-8')
            os.mkdir(out,0o700); created=True
            st=os.stat(out,follow_symlinks=False); directory_identity=(st.st_dev,st.st_ino)
            # A consumer must regard the directory as ready only after SOURCES.json appears.
            for name in ('model.onnx','libonnxruntime.so',*LICENSE_NAMES,'SOURCES.json'):
                current=os.stat(out,follow_symlinks=False)
                if not stat.S_ISDIR(current.st_mode) or (current.st_dev,current.st_ino)!=directory_identity:
                    raise PreparationError('output directory changed during publication')
                source=work/name; identity=os.stat(source,follow_symlinks=False)
                os.link(source,out/name,follow_symlinks=False)
                published.append((out/name,(identity.st_dev,identity.st_ino)))
        return out
    except BaseException as exc:
        cleanup_errors=[]
        if created:
            try:
                current=os.stat(out,follow_symlinks=False)
                if stat.S_ISDIR(current.st_mode) and (current.st_dev,current.st_ino)==directory_identity:
                    for path,identity in reversed(published):
                        try:
                            if _same_object(path,identity): os.unlink(path)
                        except OSError as cleanup_error:
                            cleanup_errors.append(str(cleanup_error))
                    try: os.rmdir(out)
                    except OSError as cleanup_error:
                        cleanup_errors.append(str(cleanup_error))  # Preserve foreign contents.
                else:
                    cleanup_errors.append('output directory changed; left untouched')
            except FileNotFoundError:
                pass
            except OSError as cleanup_error:
                cleanup_errors.append(str(cleanup_error))
        if cleanup_errors:
            raise PreparationError(str(exc)+'; cleanup incomplete: '+'; '.join(cleanup_errors)) from exc
        if isinstance(exc,PreparationError): raise
        if isinstance(exc,(OSError,zipfile.BadZipFile,RuntimeError,EOFError,ValueError,zlib.error)):
            raise PreparationError(str(exc)) from exc
        raise

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',required=True,type=Path,help='explicit local pinned ONNX file')
    parser.add_argument('--aar',required=True,type=Path,help='explicit local pinned ONNX Runtime Android AAR')
    parser.add_argument('--output',required=True,type=Path,help='new output directory outside this checkout')
    args=parser.parse_args(argv)
    try:
        repo=Path(__file__).resolve().parents[4]
        destination=args.output.resolve(strict=False)
        if destination==repo or repo in destination.parents:
            raise PreparationError('output must be outside the checkout')
        license_dir=Path(__file__).resolve().parent/'licenses'
        for name,digest in LICENSE_SHA256.items():
            if hashlib.sha256((license_dir/name).read_bytes()).hexdigest()!=digest:
                raise PreparationError('bundled license SHA-256 mismatch: '+name)
        prepared=prepare_assets(args.model,args.aar,args.output,PINNED,license_dir,LICENSE_SHA256)
        print('Prepared verified resources:',prepared)
        return 0
    except (PreparationError,OSError) as exc:
        print('AOD resource preparation failed:',exc,file=sys.stderr)
        return 1

if __name__=='__main__': sys.exit(main())
