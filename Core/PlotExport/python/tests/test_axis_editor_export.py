"""Axis/legend geometry produced by the C++ shared planner, checked at real export edges."""
import copy
import json
import os
import re
from pathlib import Path

import numpy as np
import pytest
from sst_plot_export.exporter import build_figure, export_graph


def cpp_fixture(name):
    directory = Path(os.environ.get('AXISUI1_ARTIFACT_DIR', Path(__file__).parent / 'fixtures'))
    path = directory / name
    if not path.exists(): pytest.skip('Generate the shared C++ axis fixtures with AXISUI1_ARTIFACT_DIR')
    return json.loads(path.read_text(encoding='utf8'))


def test_cpp_preview_and_export_axis_geometry_within_point_two_mm():
    snapshot = cpp_fixture('cpp-axis-snapshot.json')
    preview = cpp_fixture('cpp-preview-geometry.json')
    expected = snapshot['axis_geometry']
    assert len(preview['strokes']) == len(expected['strokes'])
    assert len(preview['captions']) == len(expected['captions'])
    for actual, reference in zip(preview['strokes'], expected['strokes']):
        for key in ('from_pt', 'to_pt'):
            assert np.max(np.abs(np.array(actual[key])-reference[key]))*25.4/72 <= .2
        assert actual['width_pt'] == reference['width_pt']
        assert actual['style'] == reference['style']
    for actual, reference in zip(preview['captions'], expected['captions']):
        assert actual['text'] == reference['text']
        assert np.max(np.abs(np.array(actual['anchor_pt'])-reference['anchor_pt']))*25.4/72 <= .2
        assert actual['font_pt'] == reference['font_pt']
    fig = build_figure(snapshot)
    fig.canvas.draw()
    strokes = [a for a in fig.axes[0].artists if a.get_gid() == 'sst-axis-stroke']
    # Matplotlib stores Line2D in lines rather than the generic artists collection.
    strokes += [a for a in fig.axes[0].lines if a.get_gid() == 'sst-axis-stroke']
    assert len(strokes) == len(expected['strokes'])
    for artist, reference in zip(strokes, expected['strokes']):
        for index, key in enumerate(('from_pt', 'to_pt')):
            point = np.array([artist.get_xdata()[index], artist.get_ydata()[index]])
            physical = point*np.array([snapshot['width_mm'],snapshot['height_mm']])
            target = np.array(reference[key])*25.4/72
            target[1] = snapshot['height_mm']-target[1]
            assert np.max(np.abs(physical-target)) <= .2
    assert fig.axes[0].xaxis_inverted()  # Reverse survives both renderers.
    assert any('α' in text.get_text() for text in fig.texts)


@pytest.mark.parametrize('format', ['png','svg','pdf'])
def test_full_cpp_axis_and_legend_export_is_lossless(format, tmp_path):
    snapshot = cpp_fixture('cpp-axis-snapshot.json')
    before = copy.deepcopy(snapshot)
    target = tmp_path / f'axis.{format}'
    export_graph(snapshot, target, format)
    assert target.stat().st_size > 500
    assert snapshot == before
    if format == 'svg':
        text = target.read_text(encoding='utf8')
        assert 'sst-axis-stroke' in text and 'sst-legend-sample' in text
        assert '<image' not in text


def test_legend_layout_has_physical_anchor_and_preserves_hidden_entries():
    snapshot = cpp_fixture('cpp-axis-snapshot.json')
    for position in range(7):
        snapshot['legend']['position'] = position
        snapshot['legend']['outside'] = position >= 4
        snapshot['legend']['columns'] = 2
        snapshot['legend_entries'] *= 2
        snapshot['legend_entries'][0]['visible'] = False
        fig = build_figure(snapshot)
        fig.canvas.draw()
        x,y,width,height = fig._sst_legend_box_pt
        page_width,page_height = np.array([snapshot['width_mm'],snapshot['height_mm']])*72/25.4
        left,bottom,fw,fh = snapshot['data_frame']
        if position == 4: assert x == pytest.approx((left+fw)*page_width+6, abs=.2*72/25.4)
        if position == 5: assert y+height == pytest.approx((1-bottom-fh)*page_height-40, abs=.2*72/25.4)
        if position == 6: assert y == pytest.approx((1-bottom)*page_height+40, abs=.2*72/25.4)
        assert width > 0 and height > 0
        # Keep the next case at two entries, rather than exponential fixture growth.
        snapshot['legend_entries'] = snapshot['legend_entries'][:1]


def test_native_legend_geometry_survives_export_within_point_two_mm():
    snapshot = cpp_fixture('cpp-axis-snapshot.json')
    layout = cpp_fixture('cpp-preview-geometry.json')['legend_geometry']
    snapshot['legend_geometry'] = layout
    fig = build_figure(snapshot)
    fig.canvas.draw()
    assert np.max(np.abs(np.array(fig._sst_legend_box_pt)-layout['box_pt']))*25.4/72 <= .2
    sample = next(a for a in fig.artists if a.get_gid() == 'sst-legend-sample')
    assert sample.get_solid_capstyle() == sample.get_dash_capstyle() == 'butt'
    assert fig.axes[0].lines[0].get_solid_capstyle() == 'butt'
    width,height = np.array([snapshot['width_mm'],snapshot['height_mm']])*72/25.4
    x,y,_,_ = layout['box_pt']
    actual = np.array([sample.get_xdata()[0]*width,(1-sample.get_ydata()[0])*height])
    expected = np.array([x+6,y+6+layout['font_pt']/2])
    assert np.max(np.abs(actual-expected))*25.4/72 <= .2
    title = next(c for c in snapshot['axis_geometry']['captions'] if c['text'].startswith('1000/T'))
    assert y+layout['box_pt'][3] < title['anchor_pt'][1]-title['font_pt']/2


def test_scatter_legend_does_not_add_a_line():
    snapshot = cpp_fixture('cpp-axis-snapshot.json')
    snapshot['legend_entries'][0]['kind'] = 1
    snapshot['legend_entries'][0]['marker'] = 1
    fig = build_figure(snapshot)
    sample = next(a for a in fig.artists if a.get_gid() == 'sst-legend-sample')
    assert sample.get_linestyle() == 'None'


def test_export_dash_lengths_stay_in_points_when_width_changes(tmp_path):
    snapshot = cpp_fixture('cpp-axis-snapshot.json')
    snapshot['series'][0]['style'].update(line_style=1, line_pt=2.5)
    target = tmp_path/'dash.svg'
    export_graph(snapshot,target,'svg')
    patterns = [tuple(map(float,p.split(','))) for p in re.findall(r'stroke-dasharray: ([^;\"]+)',target.read_text(encoding='utf8'))]
    assert (6.,3.) in patterns
    assert (15.,7.5) not in patterns


@pytest.mark.parametrize('reverse',[False,True])
def test_reciprocal_data_matches_native_implot_points(reverse):
    snapshot = cpp_fixture('cpp-axis-snapshot.json')
    expected = np.array(cpp_fixture('cpp-preview-geometry.json')['series_points_pt'])
    snapshot['x_axis']['spec']['reverse'] = reverse
    if not reverse:
        left,_,width,_ = snapshot['data_frame']
        expected[:,0] = (2*left+width)*snapshot['width_mm']*72/25.4-expected[:,0]
    fig = build_figure(snapshot)
    fig.canvas.draw()
    series = snapshot['series'][0]
    actual = fig.axes[0].transData.transform(np.column_stack([series['x'],series['y']]))
    actual[:,1] = fig.bbox.height-actual[:,1]
    actual *= 72/fig.dpi
    assert np.max(np.abs(actual-expected))*25.4/72 <= .2
