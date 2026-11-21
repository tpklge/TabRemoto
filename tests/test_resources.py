"""Public package guards: corruption, path escape and private-file exclusion."""
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package_release',ROOT/'tools/package_release.py')
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)

class Resources(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/'TabRemoto'
        shutil.copytree(ROOT/'sdcard/TabRemoto',self.root)
    def tearDown(self):self.temp.cleanup()
    def manifest(self,change):
        p=self.root/'manifest.json';data=json.loads(p.read_text());change(data);p.write_text(json.dumps(data))
    def test_valid_public_package(self):package.validate_manifest(self.root)
    def test_corrupted_resource_rejected(self):
        (self.root/'config/app.json').write_text('tampered')
        with self.assertRaises(ValueError):package.validate_manifest(self.root)
    def test_wrong_schema_rejected(self):
        self.manifest(lambda m:m.update(schema=2))
        with self.assertRaises(ValueError):package.validate_manifest(self.root)
    def test_wrong_firmware_rejected(self):
        self.manifest(lambda m:m.update(firmware='99.0.0'))
        with self.assertRaises(ValueError):package.validate_manifest(self.root)
    def test_path_escape_rejected(self):
        self.manifest(lambda m:m['files'].update({'../../private-key': '0'*64}))
        with self.assertRaises(ValueError):package.validate_manifest(self.root)
    def test_private_inventory_rejected(self):
        self.manifest(lambda m:m['files'].update({'vault/secrets.enc':'0'*64}))
        with self.assertRaises(ValueError):package.validate_manifest(self.root)
    def test_missing_resource_rejected(self):
        (self.root/'locales/pt-BR.json').unlink()
        with self.assertRaises(FileNotFoundError):package.validate_manifest(self.root)
    def test_symlink_rejected(self):
        path=self.root/'config/app.json';path.unlink();path.symlink_to(ROOT/'sdcard/TabRemoto/config/app.json')
        with self.assertRaises(ValueError):package.validate_manifest(self.root)

if __name__=='__main__':unittest.main()
