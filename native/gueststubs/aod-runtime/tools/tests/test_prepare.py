import hashlib
import importlib.util
import json
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile

MODULE = Path(__file__).resolve().parents[1] / 'prepare.py'
spec = importlib.util.spec_from_file_location('aod_prepare', MODULE)
prep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prep)

def sha(data): return hashlib.sha256(data).hexdigest()

class PreparationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='aod-prepare-test-')
        self.root = Path(self.temp.name)
        self.model = self.root/'model.onnx'; self.model.write_bytes(b'test-model')
        self.aar = self.root/'runtime.aar'
        with zipfile.ZipFile(self.aar,'w') as z:
            z.writestr('jni/arm64-v8a/libonnxruntime.so',b'test-arm64-so')
            z.writestr('../outside-must-not-be-extracted',b'bad')
        self.licenses = self.root/'licenses'; self.licenses.mkdir()
        for name in prep.LICENSE_NAMES: (self.licenses/name).write_text(name,encoding='utf-8')
        self.license_hashes={name:sha((self.licenses/name).read_bytes()) for name in prep.LICENSE_NAMES}
        self.make_spec()
        self.out = self.root/'output'
    def tearDown(self): self.temp.cleanup()
    def make_spec(self):
        m=self.model.read_bytes(); a=self.aar.read_bytes(); so=b'test-arm64-so'
        self.spec=prep.Spec(len(m),sha(m),len(a),sha(a),len(so),sha(so))
    def run_prepare(self): return prep.prepare_assets(self.model,self.aar,self.out,self.spec,self.licenses,self.license_hashes)
    def test_synthetic_success_publishes_only_known_members(self):
        self.run_prepare()
        self.assertTrue(self.out.is_dir())
        self.assertEqual((self.out/'model.onnx').read_bytes(),b'test-model')
        self.assertEqual((self.out/'libonnxruntime.so').read_bytes(),b'test-arm64-so')
        self.assertFalse((self.root/'outside-must-not-be-extracted').exists())
        info=json.loads((self.out/'SOURCES.json').read_text(encoding='utf-8'))
        self.assertEqual(info['model']['sha256'],sha(b'test-model'))
        for name in prep.LICENSE_NAMES: self.assertEqual((self.out/name).read_text(),name)
    def test_existing_output_is_never_modified(self):
        self.out.mkdir(); (self.out/'keep').write_bytes(b'untouched')
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertEqual((self.out/'keep').read_bytes(),b'untouched')
        self.assertEqual({x.name for x in self.out.iterdir()},{'keep'})
    def test_bad_model_hash_leaves_no_output(self):
        self.model.write_bytes(b'test-modeL')
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
        self.assertFalse(list(self.root.glob('.rme-aod-stage-*')))
    def test_bad_aar_hash_leaves_no_output(self):
        with self.aar.open('ab') as f: f.write(b'x')
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
    def test_same_size_aar_hash_mismatch_is_rejected(self):
        data=bytearray(self.aar.read_bytes()); data[3]^=1; self.aar.write_bytes(data)
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
    def test_changed_license_is_rejected_before_publish(self):
        (self.licenses/'MODEL_LICENSE.txt').write_text('changed',encoding='utf-8')
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
    def test_duplicate_arm64_member_is_rejected(self):
        with warnings.catch_warnings():
            warnings.simplefilter('ignore',UserWarning)
            with zipfile.ZipFile(self.aar,'a') as z: z.writestr('jni/arm64-v8a/libonnxruntime.so',b'test-arm64-so')
        self.make_spec()
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
    def test_missing_arm64_member_is_rejected(self):
        with zipfile.ZipFile(self.aar,'w') as z: z.writestr('jni/x86/libonnxruntime.so',b'wrong-abi')
        self.make_spec()
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
    def test_library_digest_mismatch_is_rejected(self):
        self.spec=prep.Spec(self.spec.model_size,self.spec.model_sha256,self.spec.aar_size,self.spec.aar_sha256,self.spec.so_size,'0'*64)
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
    def test_partial_publish_failure_removes_only_owned_output(self):
        real_link=prep.os.link; count=0
        def fail_second(*args,**kwargs):
            nonlocal count
            count+=1
            if count==2: raise OSError('simulated publication failure')
            return real_link(*args,**kwargs)
        with mock.patch.object(prep.os,'link',side_effect=fail_second):
            with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertFalse(self.out.exists())
        self.assertTrue(self.model.exists()); self.assertTrue(self.aar.exists())
    def test_interrupted_publish_cleans_owned_output(self):
        real_link=prep.os.link; count=0
        def interrupt_second(*args,**kwargs):
            nonlocal count
            count+=1
            if count==2: raise KeyboardInterrupt()
            return real_link(*args,**kwargs)
        with mock.patch.object(prep.os,'link',side_effect=interrupt_second):
            with self.assertRaises(KeyboardInterrupt): self.run_prepare()
        self.assertFalse(self.out.exists())
    def test_existing_symlink_is_not_followed(self):
        target=self.root/'unrelated'; target.mkdir(); (target/'keep').write_bytes(b'untouched')
        try: self.out.symlink_to(target,target_is_directory=True)
        except OSError: self.skipTest('directory symlink unavailable')
        with self.assertRaises(prep.PreparationError): self.run_prepare()
        self.assertEqual((target/'keep').read_bytes(),b'untouched')
    def test_cli_rejects_checkout_output_before_creating_it(self):
        destination=MODULE.parent/'must-not-be-created-by-test'
        self.assertFalse(destination.exists())
        with contextlib.redirect_stderr(io.StringIO()) as err:
            code=prep.main(['--model',str(self.model),'--aar',str(self.aar),'--output',str(destination)])
        self.assertEqual(code,1); self.assertIn('outside the checkout',err.getvalue())
        self.assertFalse(destination.exists())
    def test_cli_rejects_modified_bundled_license(self):
        with mock.patch.dict(prep.LICENSE_SHA256,{'MODEL_LICENSE.txt':'0'*64}):
            with contextlib.redirect_stderr(io.StringIO()) as err:
                code=prep.main(['--model',str(self.model),'--aar',str(self.aar),'--output',str(self.out)])
        self.assertEqual(code,1); self.assertIn('license SHA-256 mismatch',err.getvalue())
        self.assertFalse(self.out.exists())

if __name__=='__main__': unittest.main(verbosity=2)
