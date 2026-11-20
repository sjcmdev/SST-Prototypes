# View templates and bindings

`EditorLayer::GetViews()` supplies the Core `ViewHost`. Register each app template
once with a stable ID, a name, and a content renderer. The renderer receives a
`ViewInstance` and its currently resolved items. Keep scientific data in the app;
the host's catalog is a JSON snapshot with stable item/run IDs, scalar metrics,
and optional app-specific `data`. Plot renderers use UI-Library plots.

GUI controls and embedded Python use the same `view.execute` command, validation
job, and owner-thread commit. Python needs no window or OpenGL context: bind a
host to a `PanelRegistry`, `WindowWorkspace`, `CommandRegistry`, and `JobSystem`
and start the existing `PythonInterpreter`. Execute on the host's owner thread.
Validation waits up to five seconds; a paused or busy JobSystem can time out
without changing view state.
The embedded `sst_views.execute(request_json)` returns JSON with `ok` or a typed
`error` containing `code` and `message`.

```python
import json, sst_views

def execute(operation, **fields):
    result = json.loads(sst_views.execute(json.dumps(dict(operation=operation, **fields))))
    if not result['ok']:
        raise RuntimeError(result['error'])
    return result

execute('catalog', metrics=[dict(id='voc_V', name='Voc', unit='V')], items=[
    dict(id='run-1', name='Run 1', metrics=dict(voc_V=0.71)),
    dict(id='run-2', name='Run 2', metrics=dict(voc_V=0.76)),
])
execute('active', item_id='run-1')
table_id = execute('open', template_id='core.comparison', binding=dict(
    mode='explicit', item_ids=['run-1', 'run-2']))['instance_id']
# The application registered its 'iv' renderer before this call.
view_id = execute('open', template_id='iv', binding=dict(
    mode='explicit', item_ids=['run-1', 'run-2'], selection_source=table_id))['instance_id']
execute('table', instance_id=table_id, table=dict(sort_metric_id='voc_V',
    descending=True, filter_text='', selected_item_ids=['run-2']))
assert execute('rows', instance_id=view_id)['items'][0]['id'] == 'run-2'
saved = execute('save')['layout']
execute('load', layout=saved)
```

Operations: `catalog`, `active`, `open`, `bind`, `table`, `rows`, `list`,
`duplicate`, `close`, `activate`, `save`, `load`. `activate` reopens an existing
instance. `bind` replaces the binding; `table` replaces
table settings (omitted fields use defaults). Bindings are `follow_active`
(no IDs), `pinned` (one ID), or `explicit` (zero or more IDs). `selection_source`
optionally connects an explicit plot binding to a comparison instance. A missing
item stays in the binding and resolves to no data until it becomes available.

Comparison filters match item names and IDs, case insensitively. Sorting is
numeric with missing values last in either direction; an empty metric ID sorts
by name. `selected_item_ids=null` includes all filtered rows; `[]` includes none.
Table selection intersects the receiving view's binding; other views are
independent. Closing hides a window and retains its state/data, like the existing
workspace. Duplicate copies binding and table settings without copying app data.

`WindowWorkspace` owns docking, geometry, names, window IDs, and persistence.
Per-window state includes bindings, filters, sorting, and selection. Register
templates before loading the layout; factories restore instances without seed
windows. Catalog data and the active item belong to the application's project
state and must be supplied by the app. Deleted items remain safely unresolved.
Scalar metric IDs should include units (e.g. `voc_V`); descriptors always carry
an explicit `unit`, using an empty string for dimensionless values. Values may be
finite numbers or null. The existing ImGui table limit permits 63 metric columns.

WindowChromeLab demonstrates I-V and Bands templates, follow/pinned/overlay
instances, comparison selection, and an active-run menu. Use **Window → New view**
to add instances; existing title/tab actions duplicate, close, and dock them.
Application migrations are deferred to slview1 and clview1.
