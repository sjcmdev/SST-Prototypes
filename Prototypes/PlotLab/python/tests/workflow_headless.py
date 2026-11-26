"""Real native host: imports/hooks -> numerical analysis -> maps/templates -> all file edges."""
import json
import os
from pathlib import Path
import tempfile
import numpy as np
from PIL import Image
from sst_plotlab import WorksheetSession
from sst_plotlab.analysis import NamedModel

root=Path(tempfile.mkdtemp(prefix='plotint1-e2e-',dir=os.environ['PLOTINT1_RUN']))
s=WorksheetSession()
recipe=dict(name='Map import',version='1',delimiter=',',header=1,units_row=1,
    mapping=[dict(source='Row',name='Row',role=1,units='s'),dict(source='X',name='X',role=1,units='um'),dict(source='Y',name='Y',role=2,units='um'),dict(source='Z',name='Z',role=3,units='A')],
    hooks={
        'pre_parse':dict(source='def hook(data, context, settings):\n return b"".join(data.splitlines(keepends=True)[1:])'),
        'column':dict(source='def hook(column, context, settings):\n if column["name"]=="Z": column["values"] *= .001\n return column'),
        'post_import':dict(source='def hook(table, context, settings):\n assert len(table["columns"]) == 4\n return table'),
        'validation':dict(source='def hook(table, context, settings):\n assert table["columns"][3]["units"] == "A"'),
        'naming':dict(source='def hook(table, name, context, settings):\n return name+"_accepted"')})
paths=[root/'grid-a.csv',root/'grid-b.csv']
for path in paths:path.write_text('# remove\nRow,X,Y,Z\ns,um,um,A\n0,0,0,1\n1,1,0,2\n2,2,0,3\n3,0,1,4\n4,1,1,5\n5,2,1,6\n')
original=[p.read_bytes() for p in paths]
s.trust_import_recipe(recipe)
p=s.import_preview(paths,recipe)
assert p['ok'] and all(i['ready'] for i in p['items']),p
assert p['items'][0]['executed_hooks']==['pre_parse','column','post_import','validation','naming']
committed=s.import_commit();sheets=[i['worksheet_id'] for i in committed['items']]
sheet=s.sheets[0];columns={c.name:c.id for c in sheet.columns}
r=dict(id='line-fit',version=1,worksheet_id=sheets[0],x_column_id=columns['Row'],y_column_id=columns['Z'],settings=dict(operation='linear'))
before=s._session.undo_depth
result=s.analysis.run(r)
assert result['stage']=='preview_ready',result
item=result['items'][0]
assert item['error_estimate']=='available'
status=s.analysis.status(result['job_id']);assert status['items'][0]['point_count']==6 and 'x_x_units' not in status['items'][0]
assert s.analysis.status(result['job_id'],include_arrays=True)['items'][0]['x_x_units']==[0,1,2,3,4,5]
np.testing.assert_allclose(item['parameter_values_parameter_units'],[.001,.001],atol=1e-14)
np.testing.assert_allclose(item['residual_y_units'],np.zeros(6),atol=1e-14)
assert item['used_points']==6 and s._session.undo_depth==before
s.analysis.accept(result['job_id']);assert s._session.undo_depth==before+1
batch_recipes=[]
for source in sheets:
    current=next(sheet for sheet in s.sheets if sheet.id==source);ids={c.name:c.id for c in current.columns}
    batch_recipes.append(dict(id=f'fit-{source}',recipe=dict(r,id=f'fit-{source}',worksheet_id=source,x_column_id=ids['Row'],y_column_id=ids['Z'],settings=dict(operation='linear',include_curves=False))))
summary=s.analysis.batch(batch_recipes)
assert summary['stage']=='preview_ready' and len(summary['items'])==2,summary
for item in summary['items']:
    np.testing.assert_allclose(item['parameter_values_parameter_units'],[.001,.001],atol=1e-14)
    assert item['x_x_units']==[] and item['residual_y_units']==[]
model=NamedModel('line-user',1,'s','A',('offset','slope'),lambda x,offset,slope:offset+slope*x)
user=dict(r,settings=dict(operation='user',user_model_id='line-user',parameters=[dict(name='offset',units='A',start_parameter_units=.001),dict(name='slope',units='A/s',start_parameter_units=.001)]))
fitted=s.analysis.run(user,model=model)
assert fitted['stage']=='preview_ready',fitted
np.testing.assert_allclose(fitted['items'][0]['parameter_values_parameter_units'],[.001,.001],atol=1e-10)
model_file=root/'named-model.py'
model_file.write_text('from sst_plotlab.analysis import NamedModel\nmodel=NamedModel("file-line",1,"s","A",("offset","slope"),lambda x,offset,slope:offset+slope*x)\nparameters=[dict(name="offset",units="A",start_parameter_units=.001),dict(name="slope",units="A/s",start_parameter_units=.001)]\n')
loaded=s.analysis.load_model(model_file)
fitted=s.analysis.run(dict(user,settings=loaded['settings']))
assert fitted['stage']=='preview_ready',fitted
np.testing.assert_allclose(fitted['items'][0]['parameter_values_parameter_units'],[.001,.001],atol=1e-10)
model_file.write_text(model_file.read_text().replace('name="offset"','name="wrong"'))
try:s.analysis.load_model(model_file)
except Exception as error:assert 'parameter names' in str(error)
else:raise AssertionError('Mismatched file model parameters accepted')
for operation in ('interpolate','mean3','sg5','differentiate','integrate','constant_baseline','linear_baseline'):
    settings=dict(operation=operation)
    if operation=='interpolate':settings['target_x_units']=[.5,1.5,2.5]
    if operation.endswith('baseline'):settings['anchors']=[dict(x_x_units=0,y_y_units=.001)] if operation=='constant_baseline' else [dict(x_x_units=0,y_y_units=.001),dict(x_x_units=5,y_y_units=.006)]
    out=s.analysis.run(dict(r,settings=settings));assert out['stage']=='preview_ready',(operation,out)
    values=out['items'][0]['fitted_y_units']
    if operation=='differentiate':np.testing.assert_allclose(values,np.full(6,.001),atol=1e-14)
    if operation=='interpolate':np.testing.assert_allclose(values,[.0015,.0025,.0035],atol=1e-14)
for operation in ('exponential','gaussian'):
    curve=s.sheets.new(operation)
    x=np.linspace(0,8,100)
    truth=[.3,4.,1.7] if operation=='exponential' else [.3,4.,3.,.7]
    y=.3+4*np.exp(-x/1.7) if operation=='exponential' else .3+4*np.exp(-.5*((x-3)/.7)**2)
    cx=curve.add_column('Time',x,units='s',role='X');cy=curve.add_column('Signal',y,units='V')
    names=['baseline','amplitude','tau'] if operation=='exponential' else ['baseline','amplitude','center','sigma']
    parameters=[dict(name=name,units='V' if index<2 else 's',start_parameter_units=value*.9,lower_parameter_units=-10 if index<2 else .01,upper_parameter_units=10) for index,(name,value) in enumerate(zip(names,truth))]
    out=s.analysis.run(dict(id=operation,version=1,worksheet_id=curve.id,x_column_id=cx.id,y_column_id=cy.id,settings=dict(operation=operation,parameters=parameters)))
    assert out['stage']=='preview_ready',out
    np.testing.assert_allclose(out['items'][0]['parameter_values_parameter_units'],truth,atol=2e-4)
peak=s.sheets.new('Peaks');px=peak.add_column('Wavelength',[-2,-1,0,.5,2],units='nm',role='X');py=peak.add_column('Counts',[1,3,5,3,1],units='counts')
out=s.analysis.run(dict(id='peak-report',version=1,worksheet_id=peak.id,x_column_id=px.id,y_column_id=py.id,settings=dict(operation='peaks',anchors=[dict(x_x_units=0,y_y_units=1)],include_curves=False)))
assert out['stage']=='preview_ready',out
assert out['items'][0]['peak_x_x_units']==[0] and out['items'][0]['peak_fwhm_x_units']==[1.5]
assert out['items'][0]['x_x_units']==[]
accepted=s.analysis.accept(out['job_id']);assert len(accepted['worksheet_ids'])==1
map_request=dict(worksheet_id=sheets[0],x_column_id=columns['X'],y_column_id=columns['Y'],z_column_id=columns['Z'],grid_columns=3,log_color=True,contour_levels_z_units=[.0025,.0045],name='Measured map')
response=s.create_map(map_request);assert response['ok'],response
graph=response['result']['graph_id']
assert not s.export_figure(graph,paths[0],'png')['ok']
for format in ('png','jpg','svg','pdf'):
    result=s.export_figure(graph,root/f'map.{format}',format,dpi=100);assert result['ok'],result
    assert (root/f'map.{format}').stat().st_size>100
with Image.open(root/'map.jpg') as image:
    assert image.size==(315,236) and image.mode=='RGB'
assert s.export_figure(graph,root/'physical.jpg','jpg',dpi=100,width_mm=90,height_mm=70,background_rgb=(.2,.4,.6))['ok']
with Image.open(root/'physical.jpg') as image:
    assert image.size==(354,276)
    np.testing.assert_allclose(image.getpixel((0,0)),[51,102,153],atol=2)
for format in ('csv','tsv','dat'):
    response=s.export_data(dict(worksheet_id=sheets[0],column_ids=list(columns.values()),row_indices=list(range(6)),destination=str(root/f'values.{format}'),format=format,mask_policy='missing',missing_policy='nan'))
    assert response['ok'],response
    values=np.loadtxt(root/f'values.{format}',delimiter=',' if format=='csv' else '\t',skiprows=2)
    np.testing.assert_allclose(values[:,3],np.arange(1,7)*.001,atol=1e-14)
template=root/'map.plottemplate';assert s.save_graph_template(graph,template)['ok']
for format in ('png','jpg','svg','pdf'):
    batch=s.template_batch(template,[dict(worksheet_id=sheet,destination=str(root/f'batch-{sheet}.{format}')) for sheet in sheets],format=format)
    assert batch['ok'] and batch['result']['valid'],batch
    assert len(batch['result']['status']['graph_ids'])==2 and not batch['result']['status']['errors'],batch
collision=s.template_batch(template,[dict(worksheet_id=sheets[0],destination=str(root/f'batch-{sheets[0]}.png'))])
assert collision['ok'] and not collision['result']['valid'],collision
pixels=np.array([[1,2,3],[4,5,6]],dtype=np.uint16);image_file=root/'asymmetric-λ.tif';Image.fromarray(pixels).save(image_file);image_bytes=image_file.read_bytes()
image=s.import_image(image_file,dict(first_x_x_units=10,first_y_y_units=20,step_x_x_units=2,step_y_y_units=-3,x_units='um',y_units='um',z_units='counts'))
assert image['ok'],image
image_sheet=next(sheet for sheet in s.sheets if sheet.name=='Pixels')
image_request=dict(worksheet_id=image_sheet.id,column_ids=[c.id for c in image_sheet.columns],row_indices=list(range(6)),destination=str(root/'pixels.tsv'),format='tsv',mask_policy='missing')
assert s.export_data(image_request)['ok']
image_data=np.loadtxt(root/'pixels.tsv',delimiter='\t',skiprows=2)
np.testing.assert_allclose(image_data[:,0],[10,12,14,10,12,14]);np.testing.assert_allclose(image_data[:,1],[20,20,20,17,17,17]);np.testing.assert_allclose(image_data[:,2],pixels.ravel())
assert not s.export_data(dict(image_request,destination=str(image_file),format='csv'))['ok']
assert image_file.read_bytes()==image_bytes
script=f'''from sst_plotlab import WorksheetSession
from pathlib import Path
active=WorksheetSession(active=True)
result=active.analysis.run({r!r})
assert result["stage"]=="preview_ready",result
active.analysis.accept(result["job_id"])
assert active.export_figure({graph!r}, {str(root/'nested.png')!r}, "png")["ok"]
assert active.export_data({dict(worksheet_id=sheets[0],column_ids=list(columns.values()),row_indices=[0,1],destination=str(root/'nested.tsv'),format='tsv')!r})["ok"]
result=active.template_batch({str(template)!r}, [dict(worksheet_id={sheets[0]},destination={str(root/'nested-batch.png')!r})])
assert result["ok"] and not result["result"]["status"]["errors"],result
'''
nested=s.run(script,timeout_s=30);assert nested['ok'],nested
assert len(s.analysis_recipes)==3
project=root/'integrated.plotlab';s._session.save(str(project))
reopened=WorksheetSession();reopened._session.open(str(project));assert len(reopened.sheets)==len(s.sheets)
assert reopened.import_recipes==s.import_recipes
assert reopened.analysis_recipes==s.analysis_recipes and len(reopened.analysis_recipes)==3
assert reopened._session.analysis_status(123)['ok'] is False
assert s.create_map(dict(map_request,grid_columns=4))['ok'] is False
assert s.create_map(dict(map_request,grid_colums=3))['ok'] is False
assert not s._session.analysis_submit(dict(r,settings=dict(operation='peaks',anchors=[dict(x_x_units=float('nan'),y_y_units=1)])),None,False)['ok']
assert [p.read_bytes() for p in paths]==original
json.dumps(fitted,allow_nan=False)
(root/'evidence.json').write_text(json.dumps(dict(fit=item,batch=batch,image=image),indent=2))
print('plotint1 native workflow passed',flush=True)
