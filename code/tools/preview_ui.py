"""Render real Thumb UI framebuffer output to a PNG for pixel-level review."""
import pathlib
import struct
from PIL import Image, ImageDraw
from test_firmware import Firmware

root=pathlib.Path(__file__).resolve().parents[1]
out=root/'_reports'/'ui'
out.mkdir(parents=True,exist_ok=True)
f=Firmware()
data=0x20001800
f.uc.mem_write(data,struct.pack('<8H8IB3x',*([0]*8),3300,5010,3790,4060,3510,483,105,970,1))
f.call('UI_Init',0)
frames=[]
def capture(name,time=2000):
    f.call('UI_Draw',data,time)
    raw=bytes(f.uc.mem_read(f.symbols['buffer'],1024))
    img=Image.new('RGB',(128,64),(8,14,22))
    px=img.load()
    for y in range(64):
        for x in range(128):
            if raw[(y//8)*128+x] & (1<<(y&7)): px[x,y]=(221,243,255)
    img.save(out/(name+'.png'))
    frames.append((name,img))
capture('01-boot',650)
capture('02-home')
f.call('UI_Key',4);capture('03-detail')
f.call('UI_Key',4);capture('04-menu')
f.call('UI_Key',4);f.call('UI_Key',2);capture('05-current')
f.call('UI_Key',8);f.call('UI_Key',2);f.call('UI_Key',2);f.call('UI_Key',4);capture('06-about')
f.call('UI_Key',8);f.call('UI_Key',8);f.call('UI_Key',8)
f.uc.mem_write(data+48,b'\x00');capture('07-adc-error')
canvas=Image.new('RGB',(2*552,4*306),(23,28,38));draw=ImageDraw.Draw(canvas)
for i,(name,img) in enumerate(frames):
    x=20+(i%2)*552;y=12+(i//2)*306
    draw.text((x,y),name,fill=(215,224,235))
    canvas.paste(img.resize((512,256),Image.Resampling.NEAREST),(x,y+24))
canvas.save(out/'ui-preview.png')
print(out/'ui-preview.png')
