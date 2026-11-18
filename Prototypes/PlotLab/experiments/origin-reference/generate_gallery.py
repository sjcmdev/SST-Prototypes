"""Origin 2024 gallery. Run with uv --no-project --with originpro --with pillow.

All output is local to this directory. No application preferences are changed.
"""
import csv
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STEMS = ['01-log10','02-engineering','03-reciprocal','04-linked-top',
 '05-tick-in','06-tick-out','07-tick-both','08-minor-1','09-minor-4',
 '10-minor-9','11-box','12-L-only','13-zero','14-reversed','15-custom-ticks',
 '16-grid','17-group-list','18-group-T','19-template','20-grid-2x2','21-stack',
 '22-inset','23-break-one','24-break-two','25-twin-Y','26-rich-text']


def rows(name):
    with (ROOT/'fixtures'/name).open(newline='', encoding='utf-8-sig') as stream:
        return list(csv.DictReader(stream))


def read_number(op, expression):
    try:
        value = op.lt_float(expression)
        return value if math.isfinite(value) else None
    except Exception:
        return None


def unavailable(reason):
    return {'value': None, 'reason': reason}


def load_sheet(op, filename, name):
    data = rows(filename)
    sheet = op.new_sheet('w', lname=name)
    for index, field in enumerate(data[0]):
        values = [row[field] for row in data if row[field] != '']
        try:
            values = [float(v) for v in values]
        except ValueError:
            pass
        sheet.from_list(index, values, field)
    return sheet


def execute(op, layer, script):
    layer.activate()
    op.lt_exec(script)


def remove_label(layer, name):
    label=layer.label(name)
    if label: label.remove()


def frame(op, layer, rect):
    execute(op, layer, 'layer.unit=1;' + ''.join(
        f'layer.{key}={value};' for key, value in zip(
            ('left','top','width','height'), rect)))


def format_layer(op, layer, xlabel, ylabel, font=8):
    execute(op, layer, f'layer.x.label.pt={font};layer.y.label.pt={font};'
        f'layer.x2.label.pt={font};layer.y2.label.pt={font};'
        'layer.fixed=1;layer.factor=1;layer.x.thickness=0.6;layer.y.thickness=0.6;layer.tickW=0.6;layer.tickL=3;layer.x.showAxes=1;layer.y.showAxes=1;'
        'layer.x.showLabels=1;layer.y.showLabels=1;'
        'layer.x.inc=0.5;layer.x.minorTicks=4;'
        'layer.x.ticks=5;layer.y.ticks=5;')
    for name, text in [('xb',xlabel),('yl',ylabel)]:
        label = layer.label(name)
        if label:
            label.text = text
            label.set_float('fsize',font+1)
            label.set_int('background',0)
    remove_label(layer,'legend')
    for p in layer.plot_list():
        layer.set_float(f'plot{p.index()+1}.line.width', 1)


def set_page(op, graph, width_mm, height_mm):
    graph.activate()
    # Origin stores page dimensions as integer printer dots.
    op.lt_exec(f'page.kar=0;page.updatetoprinter=0;page.autosize=2;'
        f'page.width={round(width_mm*600/25.4)};'
        f'page.height={round(height_mm*600/25.4)};')


def finish_layout(op, graph, ident):
    """Place text after every resize; Origin retains template title offsets."""
    for index, layer in enumerate(graph):
        layer.activate()
        if len(graph)==1:
            frame(op,layer,(30,16,60,60))
        if ident==4:
            frame(op,layer,(30,28,60,48))
        if ident==25:
            frame(op,layer,(30,16,40,60))
        if ident==11:
            frame(op,layer,(30,16,48,60))
        if ident==14:
            frame(op,layer,(15,16,50,60))
        if ident in (17,19):
            frame(op,layer,(30,38,60,38))
        if ident==26:
            frame(op,layer,(30,30,60,46))
        execute(op,layer,'layer.fixed=1;layer.factor=1;layer.tickL=3;layer.tickW=0.6;'
            'layer.x.thickness=0.6;layer.y.thickness=0.6;')
        def coordinate(axis,fraction):
            if read_number(op,f'layer.{axis}.reverse'):
                fraction=1-fraction
            start=read_number(op,f'layer.{axis}.from')
            end=read_number(op,f'layer.{axis}.to')
            kind=read_number(op,f'layer.{axis}.type')
            if kind==2:
                return 10**(math.log10(start)+fraction*math.log10(end/start))
            if kind==5:
                return 1/(1/start+fraction*(1/end-1/start))
            return start+fraction*(end-start)
        for name,fx,fy,rotate in [('xb',.5,-.28,0),('yl',-.40,.5,90),
                                  ('xt',.5,1.35,0),('yr',1.60,.5,90)]:
            label=layer.label(name)
            if label and label.text:
                label.set_float('fsize',7 if ident in (20,21,22) else 9)
                label.set_int('background',0)
                label.set_float('rotate',rotate)
                label.set_float('x1',coordinate('x',fx))
                label.set_float('y1',coordinate('y',fy))
        if ident==18:
            execute(op,layer,'layer.factor=.2;layer.x.label.pt=40;layer.y.label.pt=40;'
                'layer.x.thickness=3;layer.y.thickness=3;layer.tickW=3;layer.tickL=15;'
                'spectrum1.barthick=80;spectrum1.dy=.015;'
                'spectrum1.left=.35*page.width;spectrum1.top=.19*page.height;')
            for name in ('xb','yl'):
                label=layer.label(name)
                if label: label.set_float('fsize',45)
            for p in layer.plot_list():
                layer.set_float(f'plot{p.index()+1}.line.width',5)
        legend=layer.label('legend')
        if legend:
            legend.set_float('fsize',4.5)
            legend.set_int('background',0)
            legend.set_float('x1',coordinate('x',.04))
            legend.set_float('y1',coordinate('y',1.8 if ident in (17,19) else 1.5 if ident==26 else .95))
        if ident==14:
            label=layer.label('yl')
            label.set_float('x1',coordinate('x',1.6))
        if ident==26:
            note=layer.label('Text')
            if note:
                note.set_float('x1',coordinate('x',.7))
                note.set_float('y1',coordinate('y',.9))


def ivplots(layer, sheet, count=1, kind='line'):
    return [layer.add_plot(sheet,2*i+1,2*i,type=kind) for i in range(count)]


def configure(op, ident, graph, sheets):
    layer=graph[0]
    iv,cv,arr,ticks,second=sheets
    if ident in (3,4):
        layer.add_plot(arr,2,0)
        layer.rescale()
        format_layer(op,layer,'T (K)',r'Rate (s\+(-1))')
        layer.set_xlim(180,340,40)
    elif ident in (1,13) or ident>=17 and ident!=25:
        ivplots(layer,second if ident==19 else iv,16 if ident in (17,18,19,26) else 1,
                'linesymbol' if ident in (17,19) else 'line')
        layer.rescale()
        format_layer(op,layer,'Voltage (V)','Current (A)')
        layer.set_xlim(-0.02,0.92,0.2) if ident==1 else layer.set_xlim(-0.1,3.4,1)
    else:
        layer.add_plot(cv,1,0)
        layer.rescale()
        format_layer(op,layer,'Voltage (V)','Capacitance (F)')
        layer.set_xlim(-1,1,0.5)
    frame(op,layer,(22,14,68,64))
    if ident==1:
        execute(op,layer,'layer.y.type=2;layer.y.from=1e-9;layer.y.to=0.1;'
            'layer.y.inc=2;layer.y.minorTicks=8;layer.y.label.numFormat=2;')
    elif ident==2:
        execute(op,layer,'layer.y.label.numFormat=3;')
    elif ident==3:
        execute(op,layer,'layer.x.type=5;layer.x.ticksbydata$="180 220 280 340";')
    elif ident==4:
        top=graph.add_layer(1)
        format_layer(op,top,'','')
        frame(op,top,(22,14,68,64))
        execute(op,top,'layer.link=1;layer.x.link=2;'
            'layer.x.link.from$="1000/X1";layer.x.link.to$="1000/X2";'
            'layer.y.link=1;layer.x.type=5;layer.x.showAxes=2;'
            'layer.y.showAxes=0;layer.x.showLabels=2;layer.y.showLabels=0;'
            'layer.x.inc=0.5;layer.x2.label.pt=8;')
        label=top.label('xt')
        if label:
            label.text=r'1000/T (K\+(-1))';label.set_float('fsize',9)
        remove_label(top,'legend')
    elif ident in (5,6,7):
        execute(op,layer,f'layer.x.ticks={dict(zip((5,6,7),(5,10,15)))[ident]};'
            f'layer.y.ticks={dict(zip((5,6,7),(5,10,15)))[ident]};')
    elif ident in (8,9,10):
        execute(op,layer,f'layer.x.minorTicks={dict(zip((8,9,10),(1,4,9)))[ident]};')
    elif ident==11:
        execute(op,layer,'layer.x.showAxes=3;layer.y.showAxes=3;'
            'layer.x.showLabels=1;layer.y.showLabels=3;'
            'layer.x2.ticks=0;layer.y2.ticks=10;')
    elif ident==13:
        execute(op,layer,'layer.x.postype=2;layer.x.position=0;'
            'layer.y.postype=2;layer.y.position=0;')
    elif ident==14:
        execute(op,layer,'layer.x.reverse=1;layer.y.postype=1;layer.y.position=0;')
    elif ident==15:
        execute(op,layer,f'range tk=[{ticks.get_book().name}]1!1;'
            'layer.x.ticksbydata$=$(tk);'
            f'layer.x.label.type=8;layer.x.label.dataset$={ticks.get_book().name}_B;')
        execute(op,layer,'layer.x.label.pt=6;')
    elif ident==16:
        for axis in ('x','y'):
            execute(op,layer,f'layer.{axis}.showGrids=3;'
                f'layer.{axis}.grid.majorType=2;layer.{axis}.grid.minorType=3;'
                f'layer.{axis}.grid.majorWidth=0.6;layer.{axis}.grid.minorWidth=0.3;'
                f'layer.{axis}.grid.majorColor=color(160,160,160);'
                f'layer.{axis}.grid.minorColor=color(215,215,215);')
    elif ident in (17,18,19,26):
        if ident!=18:
            layer.group()
        first=layer.plot_list()[0]
        if ident!=18:
            first.colorinc=1
        if ident in (17,19):
            first.symbol_kind=3;first.symbol_kindinc=1
            first.shapelist=[3,2,1,4]
            first.set_cmd('-gm 1')
            for i,p in enumerate(layer.plot_list()):
                p.symbol_size=2
                p.set_cmd(f'-d {i%3}', '-skip 12')
        if ident==18:
            # The seed retains native By Plots / Column Label Row T / Color Mapping.
            remove_label(layer,'Spectrum1')
            layer.group()
            first=layer.plot_list()[0]
            first.colorinc=0
            layer.set_int('plot1.color',5245540)
            first.zlevels={'minors':3,'levels':list(range(180,341,40))}
            execute(op,layer,'spectrum;')
        if ident in (17,19):
            execute(op,layer,'legend;')
            layer.label('legend').text='\n'.join(
                r'\l(%d) %%(%d,@LD1) K    \l(%d) %%(%d,@LD1) K'%(i,i,i+8,i+8)
                for i in range(1,9))
        if ident==26:
            layer.label('xb').text=r'Voltage, \g(D)V (V)'
            layer.label('yl').text=r'Current I\-(SC) (A)'
            note=layer.add_label(r'\g(a) = 10\+(-3) V\+(-1)')
            note.set_float('fsize',7)
            execute(op,layer,'legend;')
            legend=layer.label('legend')
            legend.text='\n'.join(r'\l(%d) %%(%d,@LL) [%%(%d,@LU)], T=%%(%d,@LD1) K'%(i,i,i,i) for i in (1,8,16))
            legend.set_float('fsize',6)
            legend.set_int('background',0)
    elif ident in (20,21):
        for index in range(1,4 if ident==20 else 3):
            child=graph.add_layer(0)
            child.add_plot(iv,2*index*4+1,2*index*4)
            child.rescale()
            format_layer(op,child,'Voltage (V)','Current (A)',6)
            execute(op,child,'layer.link=1;layer.x.link=2;layer.x.link.from$="X1";'
                'layer.x.link.to$="X2";layer.x.inc=1;')
        for i,l in enumerate(graph):
            if ident==20:
                frame(op,l,(18+(i%2)*45,10+(i//2)*45,32,30))
            else:
                frame(op,l,(22,8+i*27,68,20))
            execute(op,l,'layer.x.label.pt=6;layer.y.label.pt=6;')
            if ident==21 and i<2:
                execute(op,l,'layer.x.showLabels=0;')
                remove_label(l,'xb')
    elif ident==22:
        child=graph.add_layer(0)
        child.add_plot(iv,1,0);child.rescale()
        format_layer(op,child,'','',6)
        child.set_xlim(2,2.7,0.2);child.set_ylim(0.005,0.025,0.01)
        frame(op,child,(32,23,30,28))
        child.set_int('color',op.ocolor('white'))
        for x1,y1,x2,y2 in ((2,.005,2.7,.005),(2.7,.005,2.7,.025),
                             (2.7,.025,2,.025),(2,.025,2,.005)):
            line=layer.add_line(x1,y1,x2,y2)
            line.color='red'
            line.width=.6
    elif ident in (23,24):
        execute(op,layer,f'layer.x.breaks.enable=1;layer.x.breaks.count={ident-22};'
            'layer.x.break1.from=0.8;layer.x.break1.to=1.6;layer.x.break1.pos=35;'
            'layer.x.inc=0.5;layer.x.minorTicks=1;'
            'layer.x.break1.inc=0.5;layer.x.break1.minorTicks=1;')
        if ident==24:
            execute(op,layer,'layer.x.break2.from=2.1;layer.x.break2.to=2.5;'
                'layer.x.break2.pos=70;layer.x.break2.inc=0.5;layer.x.break2.minorTicks=1;')
    elif ident==25:
        right=graph.add_layer(2)
        right.add_plot(cv,2,0);right.rescale()
        format_layer(op,right,'',r'1/C\+(2) (F\+(-2))')
        frame(op,right,(22,14,56,64));frame(op,layer,(22,14,56,64))
        execute(op,right,'layer.link=1;layer.x.link=1;layer.y.link=0;'
            'layer.x.showAxes=0;layer.x.showLabels=0;layer.y.showAxes=2;'
            'layer.y.showLabels=2;layer.y2.label.pt=8;')
        remove_label(right,'xb');remove_label(right,'yl')
        label=right.label('yr')
        if label:
            label.text=r'1/C\+(2) (F\+(-2))';label.set_float('fsize',9)
        right.plot_list()[0].color='red'
    return graph


def capture(op, graph, stem, size, export_info):
    graph.activate()
    raw={p:read_number(op,'page.'+p) for p in ('width','height','resx','resy','kar')}
    result={'schema_version':1,'id':stem,'origin_version':read_number(op,'@V'),
        'requested_page_mm':list(size),
        'page_mm':[raw['width']/raw['resx']*25.4,raw['height']/raw['resy']*25.4],
        'page_getters':raw,'page_quantization_reason':'Origin page.width/height are integer printer dots; requested millimetres are rounded to nearest dot.',
        'exports':export_info,'layers':[]}
    axis_props=['from','to','type','inc','firstTick','majorTicks','minorTicks','reverse',
        'showAxes','showLabels','showlabel','ticks','ticklength','mticklength',
        'tickthickness','postype','position','label.pt','label.font','label.type',
        'label.numFormat','showGrids','grid.majorType','grid.minorType',
        'grid.majorWidth','grid.minorWidth','grid.majorColor','grid.minorColor']
    for index,l in enumerate(graph):
        l.activate()
        layer={'id':index+1,'bounds_percent':{p:read_number(op,'layer.'+p) for p in ('left','top','width','height')},
            'bounds_unit':read_number(op,'layer.unit'),'element_scale_factor':read_number(op,'layer.factor'),
            'link':read_number(op,'layer.link'),'axes':{},'plots':[]}
        for side,axis in [('bottom','x'),('top','x2'),('left','y'),('right','y2')]:
            values={p:read_number(op,f'layer.{axis}.{p}') for p in axis_props}
            values['unavailable_getters']={p:'Origin getter returned missing/non-finite' for p,v in values.items() if v is None}
            values['effective_label_font_pt_from_getters']=values['label.pt']*layer['element_scale_factor'] if values['label.pt'] is not None else None
            values['major_positions']=unavailable('No exposed post-layout tick vector getter found in Origin 2024 LabTalk/originpro; no locator reconstruction.')
            values['minor_positions']=unavailable('No exposed post-layout minor tick vector getter found.')
            values['rendered_labels']=unavailable('Origin SVG uses glyph paths; no per-axis text output getter found. Inspect original PNG/SVG.')
            values['ticklength_unit']='Origin raw units; visual 3-unit probe corresponds to about 3 pt in this installation. Documentation layer.tickL says 0.1 pt; do not assume that conversion.'
            values['link_from_formula']=op.get_lt_str(f'layer.{axis}.link.from')
            values['link_to_formula']=op.get_lt_str(f'layer.{axis}.link.to')
            values['ticks_by_data']=op.get_lt_str(f'layer.{axis}.ticksbydata')
            values['breaks']={'enable':read_number(op,f'layer.{axis}.breaks.enable'),
                'count':read_number(op,f'layer.{axis}.breaks.count')}
            if values['breaks']['enable']:
                values['breaks']['items']=[{p:read_number(op,f'layer.{axis}.break{j}.{p}') for p in ('from','to','pos','inc','minorTicks','space')} for j in range(1,int(values['breaks']['count'] or 0)+1)]
            layer['axes'][side]=values
        for i,p in enumerate(l.plot_list(),1):
            op.lt_exec(f'string __temperature$="%({i},@LD1)";')
            layer['plots'].append({'id':i,'dataset':p.lt_range(),
                'temperature_K':op.get_lt_str('__temperature'),
                'properties':{key:read_number(op,f'layer.plot{i}.{key}') for key in ('color','colorinc','symbol.kind','symbol.kindinc','symbol.size','line.type','line.width','group')},
                'color_rgb':unavailable('Encoded color mapping/increment property is not a resolved RGB getter; see SVG strokes.'), 'color_list':p.colormap})
        layer['graph_objects']=[]
        for obj in l.obj.GraphObjects:
            name=obj.GetName()
            kind=obj.GetNumProp('type')
            if name.startswith('_') or kind not in (0,1,2):
                continue
            layer['graph_objects'].append({'name':name,'type':kind,
                'getters':{key:read_number(op,f'{name}.{key}') for key in
                    ('attach','x1','y1','x2','y2','fsize','color','width')}})
        legend=l.label('legend')
        if l.label('Spectrum1'):
            layer['color_scale']={'object':'SPECTRUM1','levels':l.plot_list()[0].zlevels,
                'font_pt':unavailable('Spectrum1.labels.fsize is unavailable in this Origin 2024 build; original template/object retained.'),
                'bounds_getters':{p:read_number(op,'spectrum1.'+p) for p in ('x1','y1','dx','dy','barthick')}}
        expanded=[]
        if legend:
            for line in legend.text.splitlines():
                op.lt_exec('string __legendexpanded$="'+line.replace('"','\\"')+'";')
                expanded.append(op.get_lt_str('__legendexpanded'))
        layer['legend']={'source':legend.text if legend else None,
            'expanded_text':expanded if legend and not any('%(' in s for s in expanded) else
                unavailable('Origin string substitution did not expand all legend tokens.') if legend else None,
            'expanded_text_source':'Origin LabTalk string assignment in active layer; rich-text control codes retained',
            'font_pt':legend.get_float('fsize') if legend else None}
        result['layers'].append(layer)
    return result


def export(op, graph, stem, size):
    from PIL import Image
    graph.activate()
    info={}
    for kind in ('png','svg','emf'):
        path=ROOT/'exports'/f'{stem}.{kind}'
        extra='tr2.PNG.dotsperinch:=600' if kind=='png' else 'tr.Advanced.Resolution:=1'
        op.lt_exec(f'expgraph -sw type:={kind} filename:="{stem}" path:="{(ROOT/"exports").as_posix()}" '
            f'overwrite:=replace tr.Margin:=2 tr1.Unit:=1 tr1.Width:={size[0]/10} {extra};')
        if not path.exists() or path.stat().st_size==0:
            raise RuntimeError(f'EXPORT_FAILED: {path}')
        if kind=='emf':
            # Origin's Keep Size EMF uses display-device physical units, not PNG dpi.
            data=path.read_bytes()
            left,top,right,bottom=struct.unpack_from('<4i',data,24)
            ratio=100*size[0]/((right-left)/100)
            op.lt_exec(f'expgraph -sw type:=emf filename:="{stem}" path:="{(ROOT/"exports").as_posix()}" '
                f'overwrite:=replace tr.Margin:=2 tr.Advanced.Resolution:=1 tr1.Unit:=3 tr1.Width:={ratio};')
        info[kind]={'file':path.relative_to(ROOT).as_posix(),'bytes':path.stat().st_size}
        if kind=='emf':
            data=path.read_bytes()
            left,top,right,bottom=struct.unpack_from('<4i',data,24)
            info[kind]['frame_mm']=[(right-left)/100,(bottom-top)/100]
            info[kind]['origin_page_ratio_percent']=ratio
            info[kind]['size_reason']='Native Origin EMF page-ratio calibration from first export header; residual device-pixel and anisotropic display-DPI rounding retained, no header edits.'
            assert all(abs(a-b)<.5 for a,b in zip(info[kind]['frame_mm'],size)),(stem,info[kind])
    with Image.open(ROOT/'exports'/f'{stem}.png') as im:
        expected=[round(mm*600/25.4) for mm in size]
        info['png'].update(size_px=list(im.size),expected_px=expected,dpi=list(im.info.get('dpi',[])))
        assert all(abs(a-b)<=2 for a,b in zip(im.size,expected)), (stem,im.size,expected)
        assert all(abs(dpi-600)<1 for dpi in im.info['dpi'])
    return info


def generate():
    import originpro as op
    for name in ('exports','ground_truth','templates','build'):
        (ROOT/name).mkdir(exist_ok=True)
    manifest=[]
    try:
        op.set_show(False)
        op.new()
        iv=load_sheet(op,'iv_temperature_pairs.csv','I-V temperature')
        second=load_sheet(op,'iv_temperature_second_dataset.csv','I-V second dataset')
        metadata=rows('curve_metadata.csv')
        for sheet in (iv,second):
            for i,row in enumerate(metadata):
                sheet.set_label(2*i,'Voltage','L');sheet.set_label(2*i,'V','U')
                sheet.set_label(2*i+1,row['long_name'],'L');sheet.set_label(2*i+1,'A','U')
                sheet.set_label(2*i+1,row['temperature_K'],'T')
        cv=load_sheet(op,'cv_mott_schottky.csv','C-V Mott Schottky')
        arr=load_sheet(op,'arrhenius.csv','Arrhenius')
        ticks=load_sheet(op,'custom_ticks.csv','Custom ticks')
        for ident,stem in enumerate(STEMS,1):
            print('RENDER',stem,flush=True)
            template=ROOT/'templates'/'iv-reference.otpu'
            source_template = str(ROOT/'templates'/'temperature-seed.otpu') if ident==18 else str(template) if ident==19 else 'line'
            graph=op.new_graph(template=source_template,lname=stem)
            set_page(op,graph,80,60)
            configure(op,ident,graph,(iv,cv,arr,ticks,second))
            for size in ((80,60),(160,120)) if ident>=17 else ((80,60),):
                if size==(160,120):
                    graph=graph.duplicate()
                    graph.lname=stem+'-160x120'
                set_page(op,graph,*size)
                finish_layout(op,graph,ident)
                suffix=stem if size==(80,60) else stem+'-160x120'
                info=export(op,graph,suffix,size)
                truth=capture(op,graph,stem,size,info)
                if ident==19:
                    truth['template_replay']={'template':'templates/iv-reference.otpu',
                        'source_figure':'17-group-list','dataset':'iv_temperature_second_dataset.csv',
                        'current_multiplier':1.2}
                (ROOT/'ground_truth'/f'{suffix}.json').write_text(json.dumps(truth,indent=2,allow_nan=False),encoding='utf-8')
                manifest.append({'id':stem,'variant':suffix,'exports':info})
            if ident==17:
                graph.activate()
                (ROOT/'templates'/'iv-reference.otpu').unlink(missing_ok=True)
                op.lt_exec(f'template_saveas template:="iv-reference" filepath:="{(ROOT/"templates").as_posix()}" loadsys:=0 asksave:=0 emf:=0;')
            if ident==18:
                graph.activate()
                (ROOT/'templates'/'temperature-native.otpu').unlink(missing_ok=True)
                op.lt_exec(f'template_saveas template:="temperature-native" filepath:="{(ROOT/"templates").as_posix()}" loadsys:=0 asksave:=0 emf:=0;')
            print('DONE',stem,flush=True)
        op.save(str(ROOT/'origin-reference.opju'))
        (ROOT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
        print('SAVED',len(manifest),'variants',flush=True)
    finally:
        op.exit()


if __name__=='__main__':
    generate()

