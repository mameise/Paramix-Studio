# The ten V3Emu icons (20 x 20 PNG, mameise) -> src/app/icons_v3.h. A missing icon is taken from T7Emu (t7/src/app/icons.h).
# python3 gen/icons.py <folder with icon_0.png .. icon_9.png (icon_06.png also works)>
import sys, os, re
from PIL import Image
d = sys.argv[1]
t7 = re.findall(r'\{([^{}]+)\}', open(os.path.join(os.path.dirname(__file__), '../../t7/src/app/icons.h')).read())
rows = []
for i in range(10):
    f = next((os.path.join(d, n) for n in ('icon_%d.png' % i, 'icon_%02d.png' % i) if os.path.exists(os.path.join(d, n))), None)
    if not f:
        rows.append(t7[i].strip()); print('icon %d: from T7Emu' % i); continue
    im = Image.open(f).convert('RGBA').resize((20, 20))
    v = []
    for y in range(20):
        b = 0
        for x in range(20):
            p = im.getpixel((x, y))
            if p[3] > 128 and sum(p[:3]) < 384: b |= 0x80000 >> x
        v.append('0x%08X' % b)
    rows.append(','.join(v)); print('icon %d: %s' % (i, os.path.basename(f)))
with open(os.path.join(os.path.dirname(__file__), '../src/app/icons_v3.h'), 'w') as o:
    o.write('/* 20x20 icons of V3Emu (bit 0x80000 = left), drawn by mameise; made by gen/icons.py */\n')
    o.write('static const unsigned int g_icons[10][20]={\n' + ''.join('  {%s},\n' % r for r in rows) + '};\n')
