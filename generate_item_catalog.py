"""Build a private menu catalog from an owned ACNH RomFS (never downloads game data)."""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import pathlib
import re
import struct
import zlib

U16 = lambda b, o=0: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o=0: struct.unpack_from('<I', b, o)[0]

class Bcsv:
    def __init__(self, path):
        self.data = pathlib.Path(path).read_bytes()
        b = self.data
        self.count, self.width, count = struct.unpack_from('<IIH', b)
        first = 28 if b[10] else 12
        if first == 28 and b[12:16] != b'VSCB':
            raise ValueError('Unsupported BCSV magic')
        offsets = [struct.unpack_from('<II', b, first+i*8) for i in range(count)]
        self.fields = {h: (o, (offsets[i+1][1] if i+1<count else self.width)-o) for i,(h,o) in enumerate(offsets)}
        self.start = first+count*8
        if self.start+self.width*self.count != len(b):
            raise ValueError(f'Truncated BCSV {path}')
    def raw(self, i, key):
        o, n = self.fields[key]
        at = self.start+self.width*i+o
        return self.data[at:at+n]
    def integer(self, i, key, signed=False):
        return int.from_bytes(self.raw(i,key), 'little', signed=signed)
    def text(self, i, key):
        return self.raw(i,key).split(b'\0')[0].decode('utf8')

def decompress_zstd(b):
    try:
        import zstandard
    except ImportError:
        pass
    else:
        size=zstandard.frame_content_size(b)
        if size < 0 or size >= 1<<30:
            raise ValueError('Invalid or oversized Zstandard frame')
        return zstandard.ZstdDecompressor().decompress(b,max_output_size=1<<30)
    dll = ctypes.util.find_library('zstd')
    if not dll:
        raise RuntimeError('zstd library not found; specify a Python environment with the installed native zstd library on PATH')
    z = ctypes.CDLL(dll)
    z.ZSTD_getFrameContentSize.argtypes = [ctypes.c_void_p,ctypes.c_size_t]
    z.ZSTD_getFrameContentSize.restype = ctypes.c_ulonglong
    size = z.ZSTD_getFrameContentSize(b,len(b))
    if size >= 1<<30:
        raise ValueError('Invalid or oversized Zstandard frame')
    output = ctypes.create_string_buffer(size)
    z.ZSTD_decompress.argtypes = [ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t]
    z.ZSTD_decompress.restype = ctypes.c_size_t
    got = z.ZSTD_decompress(output,size,b,len(b))
    if got != size:
        raise ValueError('Zstandard decompression failed')
    return output.raw[:size]

def sarc(b):
    if b[:4] != b'SARC' or b[6:8] != b'\xff\xfe':
        raise ValueError('Unsupported SARC header')
    data = U32(b,12)
    sfat = U16(b,4)
    if b[sfat:sfat+4] != b'SFAT': raise ValueError('Missing SFAT')
    count = U16(b,sfat+6)
    node = sfat+U16(b,sfat+4)
    names = node+16*count+8
    for i in range(count):
        _, flags, start, end = struct.unpack_from('<4I', b, node+16*i)
        if flags >> 24 != 1: continue
        at = names+(flags&0xffffff)*4
        name = b[at:b.index(0,at)].decode('utf8')
        if not 0 <= start <= end <= len(b)-data: raise ValueError('Invalid SARC node')
        yield name,b[data+start:data+end]

def clean_msbt(b):
    """Decode UTF16 text while skipping complete MSBT control atoms."""
    chars=[];at=0
    while at+2 <= len(b):
        c=U16(b,at);at+=2
        if c==0:break
        if c==14:
            if at+6>len(b):raise ValueError('Truncated MSBT atom')
            size=U16(b,at+4);at+=6+size
        elif c==15:
            at+=4
        elif c in (10,13):chars.append(' ')
        elif c>=32:chars.append(chr(c))
    return ''.join(chars).strip()

def msbt(b):
    if b[:8]!=b'MsgStdBn' or b[8:10]!=b'\xff\xfe' or b[12]!=1:
        raise ValueError('Unsupported MSBT encoding')
    sections={};at=32
    for _ in range(U16(b,14)):
        size=U32(b,at+4);sections[b[at:at+4]]=b[at+16:at+16+size]
        at=(at+16+size+15)&~15
    labels=sections[b'LBL1'];texts=sections[b'TXT2'];count=U32(texts)
    offsets=[U32(texts,4+i*4) for i in range(count)]
    for i in range(U32(labels)):
        n,o=struct.unpack_from('<II',labels,4+i*8)
        for _ in range(n):
            size=labels[o];o+=1;name=labels[o:o+size].decode();o+=size
            index=U32(labels,o);o+=4
            end=offsets[index+1] if index+1<count else len(texts)
            yield name,clean_msbt(texts[offsets[index]:end])

def safe(s):return s.replace('\t',' ').replace('\r',' ').replace('\n',' ')

def extract(game, output, language='EUen'):
    game=pathlib.Path(game);romfs=game/'romfs';bcsv=romfs/'Bcsv'
    src=romfs/'Message'/f'String_{language}.sarc.zs'
    files=dict(sarc(decompress_zstd(src.read_bytes())))
    names={};group_names={};body_names={};fabric_names={};categories={}
    for path,b in files.items():
        if path.startswith('Outfit/GroupName/'):
            for label,text in msbt(b):group_names[int(label)]=text
        elif path=='Remake/STR_Remake_BodyColor.msbt':
            for label,text in msbt(b):body_names[label.split('_',1)[-1]]=text
        elif path=='Remake/STR_Remake_FabricColor.msbt':
            for label,text in msbt(b):fabric_names[label.split('_',1)[-1]]=text
        elif path.startswith('Item/'):
            category=re.sub(r'^STR_ItemName_\d+_', '',pathlib.Path(path).stem)
            for label,text in msbt(b):
                m=re.fullmatch(r'([^_]+)_(\d+)',label)
                if not m or not text:continue
                iid=int(m[2]);names[iid]=text+(' (forgery)' if 'Fake' in m[1] else '')
                categories[iid]=category
    for path,b in files.items():
        if not path.startswith('Outfit/GroupColor/'):continue
        for label,text in msbt(b):
            parts=label.split('_')
            if len(parts)<3:continue
            group=int(parts[0]);iid=int(parts[2])
            if group in group_names:
                names[iid]=f'{group_names[group]} ({text})';categories[iid]='Clothing'

    kinds=Bcsv(bcsv/'ItemKind.bcsv');stack={}
    for i in range(kinds.count):
        value=kinds.integer(i,0x4c9ba961,True)
        stack['Kind_'+kinds.text(i,0x87bf00e8)]=10 if value<0 else max(1,value)
    remake=Bcsv(bcsv/'ItemRemake.bcsv');remakes={}
    for i in range(remake.count):
        rid=remake.integer(i,0x54706054);iid=remake.integer(i,0xfd9af1e1)
        # BCSV stores last valid body index (0 means one colour), not a count.
        last=remake.integer(i,0xb0304b0d,True)
        bodies=list(range(max(0,last)+1)) if last>=0 else [0]
        fabrics=[0]
        for j,(a,c) in enumerate(zip([0x545f8769,0xbb9dec57,0x50aa5754,0xbf683c6a,0x5db42713,0xb2764c2d,0x5941f72e,0xb6839c10], [0xece3e00c,0x03218b32,0xe8163031,0x07d45b0f,0xe5084076,0x0aca2b48,0xe1fd904b,0x0e3ffb75])):
            if remake.integer(i,a)!=14 or remake.integer(i,c)!=14:
                if j not in fabrics:fabrics.append(j)
        variants=[]
        for body in bodies:
            for fabric in fabrics:
                body_name=body_names.get(f'{iid:05}_{body}',f'Body {body+1}')
                label=body_name
                if len(fabrics)>1:label+=' / '+fabric_names.get(f'{iid:05}_{fabric}',f'Fabric {fabric+1}')
                variants.append((label,body|(fabric<<5)))
        remakes[rid]=variants

    # Internal world-only records have no pocket semantics. Some have menu labels for editors.
    excluded={'Kind_SequenceOnly','Kind_DummyFtr','Kind_EventObjFtr','Kind_NnpcRoomMarker','Kind_PhotoStudioList','Kind_ShopTorso','Kind_GardenEditList','Kind_NpcOutfit','Kind_PlayerDemoOutfit','Kind_SmartPhone','Kind_GroundMaker','Kind_RiverMaker','Kind_CliffMaker','Kind_MyDesignObject','Kind_MyDesignTexture','Kind_CommonFabricObject','Kind_CommonFabricTexture','Kind_OneRoomBox','Kind_DummyWrapping','Kind_DummyPresentbox','Kind_DummyCardboard','Kind_DummyWrappingOtoshidama'}
    excluded.update({'Kind_DummyRecipe','Kind_DummyDIYRecipe','Kind_DummyHowtoBook','Kind_MusicMiss','Kind_HousePost','Kind_BridgeItem','Kind_SlopeItem','Kind_LicenseItem','Kind_CraftPhoneCase','Kind_HarvestDish','Kind_HandheldPennant','Kind_BigbagPresent','Kind_HousingKit','Kind_HousingKitRcoQuest','Kind_HousingKitBirdge','Kind_YutaroWisp','Kind_JohnnyQuest','Kind_PirateQuest','Kind_QuestWrapping','Kind_QuestChristmasPresentbox','Kind_LostQuest','Kind_LostQuestDust','Kind_TreasureQuest','Kind_TreasureQuestDust'})
    tools={'Kind_FishingRod','Kind_Net','Kind_Shovel','Kind_Axe','Kind_Watering','Kind_Slingshot'}
    category_names={'Ftr':'Furniture','Art':'Art','Bromide':'Photos','Clothing':'Clothing','Craft':'Materials','Dishes':'Food and drinks','DiveFish':'Sea creatures','Etc':'Other','Event':'Seasonal items','Fence':'Fences','Fish':'Fish','FishToy':'Fish models','Fossil':'Fossils','Gyroid':'Gyroids','HouseDoorDeco':'Door decorations','Insect':'Bugs','InsectToy':'Bug models','Money':'Bells','Music':'Music','Plant':'Plants','Poster':'Posters','Recipes':'Recipes','RoomFloor':'Flooring','RoomRug':'Rugs','RoomWall':'Wallpaper','Shell':'Shells','Tool':'Tools','Turnip':'Turnips','Umbrella':'Umbrellas'}
    params=Bcsv(bcsv/'ItemParam.bcsv');entries=[];excluded_items=[]
    for i in range(params.count):
        iid=params.integer(i,0x54706054);kind=params.text(i,0xfc275e86)
        if iid==0 or iid>=60000 or iid not in names or kind in excluded or iid==4200:
            excluded_items.append((iid,kind,names.get(iid,'')));continue
        rid=params.integer(i,0xcb5eb33f,True)
        maxstack=stack.get(kind,10 if kind.startswith('UnitIcon_Flw') else 1)
        variants=remakes.get(rid,[('Default',0)])
        # Fences use the UseCount halfword for colour so the Count halfword remains stack size.
        if kind=='Kind_Fence':variants=[(label,param<<16) for label,param in variants]
        entries.append(dict(id=iid,name=names[iid],category=category_names.get(categories.get(iid),'Other'),max_stack=maxstack,tool=kind in tools,kind=kind,variants=variants))
    byid={e['id']:e for e in entries}
    if 5794 not in byid:
        recipe_entry=dict(id=5794,name='DIY recipe',category='Recipes',max_stack=1,tool=False,kind='Kind_DIYRecipe',variants=[])
        entries.append(recipe_entry);byid[5794]=recipe_entry
    recipes=Bcsv(bcsv/'RecipeCraftParam.bcsv')
    recipe_variants=[]
    for i in range(recipes.count):
        rid=recipes.integer(i,0x54706054);iid=recipes.integer(i,0x89a3482c)
        if iid in byid:recipe_variants.append((byid[iid]['name']+' recipe',rid))
    if recipe_variants:
        byid[5794]['variants']=sorted(recipe_variants,key=lambda x:x[0].lower());byid[5794]['category']='Recipes'
    entries.sort(key=lambda e:(e['name'].lower(),e['id']))
    assert len({e['id'] for e in entries})==len(entries)
    assert all(0<e['id']<=0x7fff and 1<=e['max_stack']<=999 and e['variants'] for e in entries)
    assert all(0<=param<=0xffffffff for e in entries for _,param in e['variants'])
    assert all(e['max_stack']==1 or not any(param&0xffff for _,param in e['variants']) for e in entries)
    lines=['ACNH_MENU_ITEMS_V1']
    for e in entries:
        lines.append('\t'.join(map(str,['I',e['id'],e['max_stack'],int(e['tool']),safe(e['category']),safe(e['name'])])))
        for label,param in e['variants']:lines.append(f'V\t{param}\t{safe(label)}')
    output=pathlib.Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    temporary=output.with_suffix(output.suffix+'.tmp')
    temporary.write_text('\n'.join(lines)+'\n',encoding='utf8')
    temporary.replace(output)
    source_paths=[src,bcsv/'ItemParam.bcsv',bcsv/'ItemKind.bcsv',bcsv/'ItemRemake.bcsv',bcsv/'RecipeCraftParam.bcsv']
    report={'items':len(entries),'variants':sum(len(e['variants']) for e in entries),'recipes':len(recipe_variants),'excluded':len(excluded_items),'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'language':language,'sources':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}}
    output.with_suffix('.provenance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2))
    print('Tools:',[(e['id'],e['name']) for e in entries if e['tool']])
    return entries,excluded_items

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game');parser.add_argument('output');parser.add_argument('--language',default='EUen')
    args=parser.parse_args();extract(args.game,args.output,args.language)
