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

    def project_load(self, path):
        return self.execute(dict(operation='project_load', path=str(path)))

    def project_save(self, path):
        return self.execute(dict(operation='project_save', path=str(path)))

    def device_list(self):
        return self.execute(dict(operation='device_list'))

    def device_add(self, **request):
        return self.execute(dict(operation='device_add', **request))

    def device_duplicate(self, device_id=None):
        return self.execute(dict(operation='device_duplicate', **({'device_id': device_id} if device_id else {})))

    def device_remove(self, device_id):
        return self.execute(dict(operation='device_remove', device_id=device_id))

    def device_select(self, device_id):
        return self.execute(dict(operation='device_select', device_id=device_id))

    def run_all(self):
        return self.execute(dict(operation='run_all'))
