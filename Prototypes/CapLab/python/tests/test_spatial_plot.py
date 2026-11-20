"""New plot kinds through the same nanobind execute -> Core command/job path as GUI export."""
import copy
import json
from pathlib import Path
import pytest
from caplab import execute, parse_lab_as, project_from_preview


def project(kind):
    p=project_from_preview(parse_lab_as('100 1e-9 1e6\n200 2e-9 1e6\n300 3e-9 1e6\n400 4e-9 1e6\n500 5e-9 1e6\n600 6e-9 1e6','cf_100K_7.dat'),100)
    engine=p['engine']
    objects=engine['objects']
    sweep=p['sweeps'][0]
    # Preserve the measurement; create an independent grid worksheet with stable IDs.
    next_id=engine['nextId']
    def reserve():
        nonlocal next_id
        result=next_id;next_id+=1;return result
    book=reserve();sheet=reserve();rows=[reserve() for _ in range(6)]
    cols=[reserve() for _ in range(3)]
    graph=reserve();layer=reserve();curve=reserve()
    def obj(id,parent,name,type,**fields):
        return dict(id=id,parent=parent,name=name,type=type,order=id,revision=0,**fields)
    objects.extend([obj(book,engine['root'],'Grid data',1),obj(sheet,book,'Uniform grid',2,rows=rows,source={})])
    values=([100,200,300]*2,[200]*3+[300]*3,[1,2,3,4,'nan',6])
    units=('Hz','K','nF')
    for id,v,u in zip(cols,values,units):
        objects.append(obj(id,sheet,u,3,role=0,units=u,longName='',comments='',errorOf=0,columnType=0,values=v))
    template=next(o for o in objects if o['type']==5)['spec']
    spec=copy.deepcopy(template)
    spec.update(x=dict(label='Frequency',unit='Hz',scale=0,grid=True),
                y=dict(label='Temperature',unit='K',scale=0,grid=True),
                z=dict(label='Capacitance',unit='nF',scale=0,grid=True),colormap='Viridis')
    camera=dict(rotation_xyzw=[0,0,0,1],axis_limits=[50,350,150,350,0,7])
    view=dict(ranges=[50,350,150,350],camera=camera)
    style=copy.deepcopy(next(o for o in objects if o['type']==6)['style'])
    objects.extend([obj(graph,engine['root'],'Spatial figure',4,widthMm=130,heightMm=100),
                    obj(layer,graph,'Axes',5,spec=spec,view=view),
                    obj(curve,layer,'Grid',6,columns=[sheet,*cols,0,0],kind=kind,style=style,visible=True,gridColumns=3,mask=[0,1,0,0,0,0])])
    engine['nextId']=next_id;p['graph']=graph
    return p,view


@pytest.mark.parametrize('kind',[4,5,6,7])
@pytest.mark.parametrize('format',['png','svg','pdf'])
@pytest.mark.parametrize('batch',[False,True])
def test_execute_exports_spatial_kinds(tmp_path,kind,format,batch):
    p,view=project(kind)
    target=tmp_path/f'figure.{format}'
    request=dict(operation='export',path=str(target),ranges=view['ranges'],view=view)
    if batch:
        request['graphs']=[dict(graph_id=p['graph'],ranges=view['ranges'],view=view)]
    result=execute(p,request)
    assert result['ok'],result.get('error')
    snapshot=result['export']
    if batch:
        target=Path(snapshot['graphs'][0]['path'])
        snapshot=snapshot['graphs'][0]['snapshot']
    assert snapshot['series'][0]['kind']==kind
    assert snapshot['series'][0]['z'][4] is None
    assert snapshot['camera']==view['camera']
    assert target.stat().st_size>1000
    json.dumps(snapshot,allow_nan=False)


def test_execute_save_open_preserves_camera_and_rejects_nonuniform(tmp_path):
    p,view=project(4)
    target=tmp_path/'camera.caplab.json'
    result=execute(p,dict(operation='save',path=str(target)))
    assert result['ok'],result.get('error')
    restored=execute(p,dict(operation='open',path=str(target)))
    assert restored['ok'],restored.get('error')
    layer=next(o for o in restored['project']['engine']['objects'] if o['type']==5 and o['parent']==p['graph'])
    assert layer['view']['camera']==view['camera']
    curve=next(o for o in p['engine']['objects'] if o['type']==6 and o['kind']==4)
    x=next(o for o in p['engine']['objects'] if o['id']==curve['columns'][1])
    x['values'][2]=350
    failed=execute(p,dict(operation='save',path=str(target)))
    assert not failed['ok']
    assert 'Non-uniform' in failed['error']['message']
