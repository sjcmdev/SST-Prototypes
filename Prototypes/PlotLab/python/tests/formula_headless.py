"""Embedded Python P6b command-path checks (fixture supplied by GTest)."""
from sst_plotlab import WorksheetSession
session = WorksheetSession()
def command(action, **kw):
    result = session.command(dict(action=action, **kw))
    assert result['ok'], result
    return result['result']
command('load_project', project=fixture)
objects = command('snapshot')['objects']
sheet = next(o['id'] for o in objects if o['type'] == 2)
x, y = [o['id'] for o in objects if o['parent'] == sheet][:2]
def value(id):
    return next(o for o in command('snapshot')['objects'] if o['id'] == id)
command('set_column_values', column_id=y, expression='source*scale+i', bindings={'source':x},
        before_script=f'scale = mean(col({x}))', row_from=2, row_to=3,
        recalculate='Manual')
assert command('recompute_status', column_id=y)['state'] in ('Running','Clean')
command('recompute')
assert value(y)['values'] == [2, 6, 9]
formula = value(y)['formula']
assert formula['evaluatorVersion'] == 2 and formula['bindings']['source'] == x
command('set_column_values', column_id=y, expression='i*10', recalculate='None')
command('recompute')
assert value(y)['values'] == [10, 20, 30]
command('undo'); command('recompute')
assert value(y)['formula'] == formula
command('redo'); command('recompute')
assert value(y)['formula']['recalculate'] == 'None'
saved = command('snapshot'); command('load_project', project=saved)
assert value(y)['formula']['recalculate'] == 'None'
for script in ['import os', 'x = open(1)', 'x = (1).__class__', 'while True: pass',
               'x = col(999999)', f'x = col({x})*1000000000000']:
    response = session.command(dict(action='set_column_values',column_id=y,
                                   expression='i',before_script=script))
    if response['ok']:
        response = session.command(dict(action='recompute'))
    assert not response['ok'], script
# A failed producer keeps cached values and exposes a row diagnostic.
command('set_column_values', column_id=y, expression='sqrt(col(A)-2)')
response = session.command(dict(action='recompute'))
assert not response['ok'] and response['error']['type'] == 'domain', response
assert value(y)['values'] == [10,20,30]
assert command('recompute_status', column_id=y)['state'] == 'Failed'
assert 'row 1' in command('recompute_status', column_id=y)['diagnostic']
command('set_column_values',column_id=x,expression='col(B)')
response = session.command(dict(action='recompute'))
assert not response['ok'] and response['error']['type']=='cycle', response
assert command('recompute_status',column_id=x)['state']=='Blocked'
command('undo')
command('set_column_values',column_id=y,expression='col(Missing)')
response = session.command(dict(action='recompute'))
assert not response['ok'] and response['error']['type']=='missing_column', response
assert command('recompute_status',column_id=y)['state']=='Blocked'
print('P6b headless: formulas, modes, range, sandbox, persistence, undo/redo, typed errors passed')

# Destruction/replacement while a script job acquires the GIL must not deadlock.
for replace in (False, True):
    transient = WorksheetSession()
    assert transient.command(dict(action="load_project",project=fixture))["ok"]
    assert transient.command(dict(action="set_column_values",column_id=y,
        expression="scale*i",before_script=f"scale = mean(col({x}))"))["ok"]
    if replace:
        assert transient.command(dict(action="load_project",project=fixture))["ok"]
    del transient

# Headless header references bind both column and parameter IDs explicitly.
command("parameter_add",worksheet_id=sheet,name="T")
parameter=value(sheet)["parameters"][-1]["id"]
command("parameter_value",column_id=x,parameter_id=parameter,value="300")
request=dict(action="set_column_values",column_id=y,expression="col(input)[T]+i",
             bindings={"col(input)[T]":x})
response=session.command(request)
assert not response["ok"] and response["error"]["type"]=="validation",response
command("set_column_values",column_id=y,expression="col(input)[T]+i",
        bindings={"col(input)[T]":x},parameter_bindings={"col(input)[T]":parameter})
command("recompute")
assert value(y)["values"]==[301,302,303]
