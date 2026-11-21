import importlib.util
import json
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('tabremoto_device', Path(__file__).resolve().parents[1] / 'tools/device.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class SerialReplies:
    in_waiting = 0
    def __init__(self, replies):
        self.replies = list(replies)
    def write(self, data):
        return len(data)
    def flush(self):
        pass
    def readline(self):
        return ('TABREMOTO ' + json.dumps(self.replies.pop(0)) + '\n').encode()

class ReplyTests(unittest.TestCase):
    def device(self, replies):
        device = module.Device.__new__(module.Device)
        device.serial = SerialReplies(replies)
        device.log = None
        device._wait_prompt = lambda: None
        return device
    def test_snapshot_does_not_accept_delayed_status_as_capture_ack(self):
        device = self.device([{'ok': True, 'uptime_ms': 123}, {'ok': True}])
        self.assertEqual(device.command('snapshot'), {'ok': True})
        self.assertEqual(device.serial.replies, [])
    def test_capture_read_skips_old_ack_before_actual_pixels(self):
        response = {'ok': True, 'data': 'YWJj'}
        device = self.device([{'ok': True}, response])
        self.assertEqual(device.command('screen-read 0'), response)

    def test_provision_challenge_skips_delayed_status(self):
        response = {'ok': True, 'key': '12' * 32, 'receipt': True}
        device = self.device([{'ok': True, 'uptime_ms': 123}, response])
        self.assertEqual(device.command('provision-open'), response)
    def test_provision_ack_skips_delayed_structured_reads(self):
        device = self.device([{'ok': True, 'uptime_ms': 123}, {'ok': True}])
        self.assertEqual(device.command('provision 00'), {'ok': True})
    def test_ui_action_waits_for_its_result(self):
        response = {'ok': True, 'result': 2}
        device = self.device([{'ok': True}, response])
        self.assertEqual(device.command('ui-action-status'), response)

if __name__ == '__main__':
    unittest.main()
