"""Native command -> typed chart -> persistence/template -> five publication formats."""
from pathlib import Path
import json
import os
import tempfile
import _sst_plotlab
from sst_plotlab import WorksheetSession

root=Path(tempfile.mkdtemp(prefix='should-fixtures-',dir=os.environ['PLOTINT1_RUN']))
(Path(os.environ['PLOTINT1_RUN'])/'should-latest.txt').write_text(str(root))
root.mkdir(exist_ok=True)
s=WorksheetSession()
sheet=s.sheets.new('Signals')
x=sheet.add_column('Time',[1,2,3,4,5,6],units='s',role='X')
y=sheet.add_column('Signal',[2,4,3,6,8,10],units='A',role='Y')
y2=sheet.add_column('Repeat',[1,2,2,3,4,5],units='A',role='Y')
err=sheet.add_column('Sigma',[.1,.2,.3,.4,.5,.6],units='A')
plus=sheet.add_column('Sigma plus',[.2,.4,.6,.8,1,1.2],units='A')
xe=sheet.add_column('Time sigma',[.01,.02,.03,.04,.05,.06],units='s')

def objects():
    response=s.command(dict(action='snapshot'));assert response['ok'],response
    return response['result']['objects']

def children(parent):
    return sorted([o for o in objects() if o['parent']==parent],key=lambda o:o['order'])

def edit(layer,**request):
    result=s.edit_plot(layer,request);assert result['ok'],result

results=[]
for kind in (8,9,10,11,12,13,14,15,16,17,18):
    response=s.plot_columns(sheet.id,[y.id,y2.id] if kind in (10,11,12,13,15,17) else [y.id],kind=kind,x_column_id=x.id,y_error_column_id=err.id if kind==18 else 0)
    assert response['ok'],response
    graph=response['graph_id'];layer=children(graph)[0]['id'];plots=children(layer);ids=[p['id'] for p in plots]
    if kind==14:edit(layer,action='style',series_ids=ids,style=dict(bins=3,density=True))
    if kind==18:edit(layer,action='errors',series_ids=ids,y_error_column_id=err.id,y_error_plus_column_id=plus.id)
    if kind==8:edit(layer,action='errors',series_ids=ids,x_error_column_id=xe.id,y_error_column_id=err.id,y_error_plus_column_id=plus.id)
    for format in ('png','jpg','svg','pdf','eps'):
        path=root/f'kind-{kind}.{format}';response=s.export_figure(graph,path,format,dpi=100);assert response['ok'],(kind,format,response)
        assert path.stat().st_size>100
    if kind==14:
        snapshot=_sst_plotlab.PlotSession(json.dumps(s.command(dict(action='snapshot'))['result'])).snapshot(graph,[0,12,0,1])
        geometry=snapshot['series'][0]['chart'];assert geometry['samples']==6 and len(geometry['paths'])==3
        mass=sum((p['points'][1][0]-p['points'][0][0])*p['points'][2][1] for p in geometry['paths']);assert abs(mass-1)<1e-12
    if kind==17:
        primary=layer;multi=graph
        inset=s.create_layer(graph,dict(name='Inset',copy_layer_id=layer,frame_page_fraction=[.55,.55,.4,.4]));assert inset['ok'],inset
        extra=inset['result']['layer_id'];assert s.edit_layer(extra,dict(shared_x_layer_id=layer))['ok']
        assert not s.edit_layer(layer,dict(shared_x_layer_id=extra))['ok']
        assert s.arrange_layers(graph,2,shared_x=True)['ok']
        colors={p['id']:p['style']['color'] for p in children(layer)}
        edit(layer,action='move',series_ids=[ids[0]],offset=1)
        assert {p['id']:p['style']['color'] for p in children(layer)}==colors
        edit(layer,action='independent_style',series_ids=[ids[0]],visible=True)
        edit(layer,action='group_style',series_ids=[ids[1]],style=dict(color=[1,0,0,1]))
        assert children(layer)[1]['style']['color']==colors[ids[0]]
        template=root/'layers.plottemplate';assert s.save_graph_template(graph,template)['ok']
        batch=s.template_batch(template,[dict(worksheet_id=sheet.id,destination=str(root/'layers-batch.eps'))],format='eps')
        assert batch['ok'] and batch['result']['valid'] and not batch['result']['status']['errors'],batch
        for format in ('png','jpg','svg','pdf','eps'):assert s.export_figure(graph,root/f'layers.{format}',format)['ok']
    results.append(dict(kind=kind,graph_id=graph,layer_id=layer))

spatial=s.sheets.new('Spatial')
sx=spatial.add_column('X',[0,1,2,0,1,2],units='um',role='X')
sy=spatial.add_column('Y',[0,0,0,1,1,1],units='K',role='Y')
sz=spatial.add_column('Z',[1,2,3,4,5,6],units='counts',role='Z')
for kind in ('heatmap','surface','scatter3d','line3d'):
    response=s.create_map(dict(worksheet_id=spatial.id,x_column_id=sx.id,y_column_id=sy.id,z_column_id=sz.id,grid_columns=3,kind=kind,name=kind))
    assert response['ok'],response
    graph=response['result']['graph_id']
    if kind in ('heatmap','surface'):
        layer=children(graph)[0]['id']
        assert s.create_layer(graph,dict(name='Spatial inset',copy_layer_id=layer,frame_page_fraction=[.55,.55,.4,.4]))['ok']
    for format in ('png','jpg','svg','pdf','eps'):
        result=s.export_figure(graph,root/f'{kind}.{format}',format);assert result['ok'],result
    results.append(dict(kind=kind,graph_id=graph))
project=root/'should.plotlab';s._session.save(str(project))
reopened=WorksheetSession();reopened._session.open(str(project))
for format in ('png','svg','pdf','jpg','eps'):
    result=reopened.export_figure(multi,root/f'reopened-layers.{format}',format);assert result['ok'],result
assert json.loads(project.read_text())['version']==11
assert (root/'kind-14.eps').read_bytes().startswith(b'%!PS-Adobe-3.0 EPSF-3.0')
(root/'evidence.json').write_text(json.dumps(results,indent=2))
print('plotshould1 native vertical slices passed',flush=True)
