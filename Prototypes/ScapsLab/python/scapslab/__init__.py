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
            if response.get('paused'):
                return dict(ok=True, paused=True, status=response['status'], progress=response.get('progress', {}), error=None)
            if not response['running']:
                return response['response']
            time.sleep(0.005)

    def parameter_bounds(self):
        """Effective bounds; numeric limits use the explicit SI unit field."""
        return self.start(dict(operation='parameter_bounds', action='inspect'))

    def library(self, action='inspect', **request):
        """Typed asset operations through the same Core command/job path as the GUI.

        Payload fields use device_project v1 units. include_data=True returns a
        selected payload; default inspection returns lightweight metadata only.
        """
        return self.execute(dict(operation='library', action=action, **request))

    def reload_parameter_bounds(self):
        return self.execute(dict(operation='parameter_bounds', action='reload'))

    def reset_parameter_bounds(self):
        return self.execute(dict(operation='parameter_bounds', action='reset'))

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

    def dlcp(self, **settings):
        """Explicit cancellable DLCP; V_max_V and amplitudes_V use V (peak-to-peak)."""
        return self.execute(dict(operation='dlcp', **settings))

    def dlts(self, variant='classic', **settings):
        """Virtual DLTS through the same command/job path as Run Actions.

        variant: classic, laplace, capture (pulse width/depth/DDLTS), optical.
        Protocol fields use SI units; voltage is left minus right.
        """
        operations = dict(classic='dlts', laplace='laplace_dlts', capture='capture_dlts', optical='optical_dlts')
        if variant not in operations:
            return dict(ok=False, error=dict(code='invalid_variant', message='Use classic, laplace, capture or optical'))
        return self.execute(dict(operation=operations[variant], **settings))

    def export_dlcp(self, path):
        return self.execute(dict(operation='export_dlcp', path=str(path)))

    def run_settings(self, actions):
        return self.execute(dict(operation='run_settings', actions=actions))

    def single_shot(self, actions=None):
        return self.execute(dict(operation='single_shot', **({'actions': actions} if actions is not None else {})))

    def capture_live(self, kind='iv'):
        return self.execute(dict(operation='capture_live', kind=kind))

    def recalculate_live(self, kind='iv'):
        return self.execute(dict(operation='recalculate_live', kind=kind))

    def select_bias(self, index):
        return self.execute(dict(operation='select_bias', index=index))

    def recalculate_stored_bias(self):
        return self.execute(dict(operation='recalculate_stored_bias'))

    def resume_actions(self):
        return self.execute(dict(operation='resume_actions'))

    def stop(self):
        return self.execute(dict(operation='cancel'))

    def calculate_missing(self, columns=None):
        return self.execute(dict(operation='record_batch', calculate_missing=True,
                                 **({'columns': columns} if columns is not None else {})))

    def results_select(self, run_id, member_index=0, attempt=0, action='iv', device_id=None):
        return self.execute(dict(operation='history_select', run_id=run_id, member_index=member_index,
                                 attempt=attempt, action=action, **({'device_id': device_id} if device_id else {})))

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

    def history_list(self, device_id=None):
        return self.execute(dict(operation='history_list', **({'device_id': device_id} if device_id else {})))

    def history_update(self, run_id, **metadata):
        return self.execute(dict(operation='history_update', run_id=run_id, **metadata))

    def history_select(self, run_id, device_id=None):
        return self.execute(dict(operation='history_select', run_id=run_id, **({'device_id': device_id} if device_id else {})))

    def history_clear(self, scope='device', device_id=None, include_pinned=False):
        return self.execute(dict(operation='history_clear', scope=scope, include_pinned=include_pinned,
                                 **({'device_id': device_id} if device_id else {})))

    def quick_sweep(self, parameter_id, delta_native_units, points=5, spacing='linear', **sampling):
        """delta_native_units uses the unit from batch_parameters(); sampling uses voltages_V/frequencies_Hz."""
        return self.execute(dict(operation='quick_sweep', parameter_id=parameter_id, delta_native_units=delta_native_units,
                                 points=points, spacing=spacing, **sampling))

    def tutorials(self):
        return self.execute(dict(operation='tutorial_list'))

    def tutorial_start(self, tutorial_id, mode='learn', resume=False):
        return self.execute(dict(operation='tutorial_start', tutorial_id=tutorial_id, mode=mode, resume=resume))

    def tutorial_state(self):
        return self.execute(dict(operation='tutorial_state'))

    def tutorial_stop(self):
        return self.execute(dict(operation='tutorial_stop'))

    def project_new(self):
        return self.execute(dict(operation='project_new'))
