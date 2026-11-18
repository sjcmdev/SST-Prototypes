"""ScapsLab embedded API. All operations use Core commands and jobs."""
import json
import time
from sst_scapslab import Session as _Session

class Session:
    def __init__(self, document=None):
        self._native = _Session(json.dumps(document))

    def start(self, request):
        return json.loads(self._native.start(json.dumps(request)))

    def poll(self):
        return json.loads(self._native.poll())

    def execute(self, request):
        response = self.start(request)
        if not response['ok']:
            return response
        while True:
            response = self.poll()
            if not response['running']:
                return response['response']
            time.sleep(0.005)
