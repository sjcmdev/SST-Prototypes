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

    def batch_parameters(self):
        return self.execute(dict(operation='batch_parameters'))

    def batch_settings(self, spec, columns=None):
        return self.execute(dict(operation='batch_settings', spec=spec,
                                 **({'columns': columns} if columns is not None else {})))

    def preview_batch(self, **request):
        return self.execute(dict(operation='preview_batch', **request))

    def run_batch(self, **request):
        return self.execute(dict(operation='run_batch', **request))

    def record_batch(self, columns=None):
        return self.execute(dict(operation='record_batch',
                                 **({'columns': columns} if columns is not None else {})))

    def retry_batch(self, member_id):
        return self.execute(dict(operation='retry_batch', member_id=member_id))

    def batch_select(self, index):
        return self.execute(dict(operation='batch_select', index=index))

    def export_batch(self, path, member_id=None):
        return self.execute(dict(operation='export_batch', path=str(path),
                                 **({'member_id': member_id} if member_id else {})))

    def send_to_plotlab(self, path, member_id=None):
        return self.execute(dict(operation='send_to_plotlab', path=str(path),
                                 **({'member_id': member_id} if member_id else {})))

    def import_def(self, path, scaps_root=''):
        return self.execute(dict(operation='import_def', path=str(path), scaps_root=str(scaps_root)))

    def export_def(self, path):
        return self.execute(dict(operation='export_def', path=str(path)))

    def load_project(self, path):
        return self.project_load(path)

    def save_project(self, path, include_results=True):
        return self.execute(dict(operation='project_save', path=str(path), include_results=include_results))

    def export_results(self, path, format='csv', run_ids=None, all=False):
        return self.execute(dict(operation='export_results', path=str(path), format=format,
                                 run_ids=run_ids or [], all=all))

    def result_runs(self):
        snapshot = self.execute(dict(operation='inspect'))
        if not snapshot['ok']:
            return snapshot
        return dict(ok=True, runs=snapshot['runs'], error=None)
