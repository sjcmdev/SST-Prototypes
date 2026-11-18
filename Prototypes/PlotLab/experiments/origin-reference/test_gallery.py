"""Checks original Origin exports and getter contracts; does not launch Origin."""
import json
from pathlib import Path
import struct
import unittest
import xml.etree.ElementTree as ET
from PIL import Image
from generate_gallery import STEMS

ROOT=Path(__file__).resolve().parent

class GalleryTests(unittest.TestCase):
    def truth(self,stem):
        return json.loads((ROOT/'ground_truth'/f'{stem}.json').read_text(encoding='utf-8'))

    def test_complete_export_set_and_physical_size(self):
        manifest=json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))
        variants=[stem+suffix for i,stem in enumerate(STEMS,1)
                  for suffix in (('', '-160x120') if i>=17 else ('',))]
        self.assertEqual([v['variant'] for v in manifest],variants)
        self.assertEqual(len(variants),36)
        for stem in variants:
            with self.subTest(stem=stem):
                truth=self.truth(stem)
                size=truth['requested_page_mm']
                self.assertTrue(all(abs(a-b)<25.4/600 for a,b in zip(size,truth['page_mm'])))
                with Image.open(ROOT/'exports'/f'{stem}.png') as im:
                    im.verify()
                with Image.open(ROOT/'exports'/f'{stem}.png') as im:
                    self.assertTrue(all(abs(a-round(mm*600/25.4))<=2 for a,mm in zip(im.size,size)))
                    self.assertTrue(all(abs(d-600)<.1 for d in im.info['dpi']))
                svg=ET.parse(ROOT/'exports'/f'{stem}.svg').getroot()
                self.assertAlmostEqual(float(svg.attrib['width'].removesuffix('cm'))*10,size[0])
                self.assertAlmostEqual(float(svg.attrib['height'].removesuffix('cm'))*10,size[1])
                emf=(ROOT/'exports'/f'{stem}.emf').read_bytes()
                self.assertEqual(struct.unpack_from('<I',emf,0)[0],1)
                self.assertEqual(emf[40:44],b' EMF')
                left,top,right,bottom=struct.unpack_from('<4i',emf,24)
                self.assertLess(abs((right-left)/100-size[0]),.5)
                self.assertLess(abs((bottom-top)/100-size[1]),.5)
                for layer in truth['layers']:
                    self.assertEqual(layer['bounds_unit'],1)
                    for axis in layer['axes'].values():
                        self.assertIsNone(axis['rendered_labels']['value'])
                        self.assertTrue(axis['rendered_labels']['reason'])

    def test_origin_feature_getters(self):
        def axis(stem,side='bottom',layer=0):
            return self.truth(stem)['layers'][layer]['axes'][side]
        self.assertEqual(axis('01-log10','left')['type'],2)
        self.assertEqual(axis('02-engineering','left')['label.numFormat'],3)
        self.assertEqual(axis('03-reciprocal')['type'],5)
        linked=axis('04-linked-top','top',1)
        self.assertEqual(linked['link_from_formula'],'1000/X1')
        self.assertAlmostEqual(linked['from'],1000/180)
        for stem,expected in [('05-tick-in',5),('06-tick-out',10),('07-tick-both',15)]:
            self.assertEqual(axis(stem)['ticks'],expected)
        for stem,count in [('08-minor-1',1),('09-minor-4',4),('10-minor-9',9)]:
            self.assertEqual(axis(stem)['minorTicks'],count)
        self.assertEqual(axis('14-reversed')['reverse'],1)
        self.assertTrue(axis('15-custom-ticks')['ticks_by_data'])
        self.assertEqual(axis('16-grid')['showGrids'],3)
        self.assertEqual(len(self.truth('17-group-list')['layers'][0]['plots']),16)
        self.assertEqual({p['properties']['line.type'] for p in self.truth('17-group-list')['layers'][0]['plots']},{1,2,3})
        mapped=self.truth('18-group-T')['layers'][0]
        self.assertEqual({p['properties']['color'] for p in mapped['plots']},{5245540})
        self.assertEqual([int(p['temperature_K']) for p in mapped['plots']],
                         [181,191,201,209,217,227,237,247,257,267,277,286,296,306,321,330])
        self.assertEqual(mapped['color_scale']['levels']['levels'],[180,220,260,300,340])
        self.assertEqual(mapped['color_scale']['levels']['minors'],3)
        self.assertEqual(axis('23-break-one')['breaks']['count'],1)
        self.assertEqual(axis('24-break-two')['breaks']['count'],2)
        for stem,count in [('20-grid-2x2',4),('21-stack',3),('22-inset',2),('25-twin-Y',2)]:
            self.assertEqual(len(self.truth(stem)['layers']),count)
        self.assertEqual(len([o for o in self.truth('22-inset')['layers'][0]['graph_objects'] if o['type']==2]),4)
        for stem in ('20-grid-2x2','21-stack'):
            layers=self.truth(stem)['layers']
            self.assertTrue(all(l['link']==1 for l in layers[1:]))
            self.assertTrue(all(l['axes']['bottom']['from']==layers[0]['axes']['bottom']['from'] and
                                l['axes']['bottom']['to']==layers[0]['axes']['bottom']['to'] for l in layers[1:]))
        self.assertTrue((ROOT/'origin-reference.opju').stat().st_size>1000)
        self.assertTrue((ROOT/'templates'/'iv-reference.otpu').stat().st_size>1000)
        self.assertEqual(self.truth('19-template')['template_replay']['current_multiplier'],1.2)

if __name__=='__main__':unittest.main()
