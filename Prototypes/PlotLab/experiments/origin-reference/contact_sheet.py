from pathlib import Path
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parent
files=sorted((root/'exports').glob('*.png'))
for batch in range(0,len(files),4):
 canvas=Image.new('RGB',(1200,950),'#dddddd');draw=ImageDraw.Draw(canvas)
 for i,path in enumerate(files[batch:batch+4]):
  im=Image.open(path).convert('RGB');im.thumbnail((590,440))
  x=(i%2)*600;y=(i//2)*475
  draw.text((x+10,y+5),path.stem,fill='black')
  canvas.paste(im,(x+(600-im.width)//2,y+25))
 canvas.save(root/'build'/f'contact-{batch//4+1:02}.png')
