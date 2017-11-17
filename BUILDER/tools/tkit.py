''' Card pack generator
json file fmt:
file = ["Card pack description","CPCKIDST", [..CARD_DATA_1..],[...]]
carddata = [rank_num,"card_name",num_up,num_right,num_down,num_left,element,"card_image_name"]

binary file fmt:
.db "TriCrPak"    ;8 character, file identifier
.db "CPCKIDST",0  ;8 chr + null terminator.
.db "Card pack description",0  ;variable width, null terminated.
.dw number_of_cards_in_pack
_data_field:
.db rank                   ;+0
.dw offset_to_name_string  ;+1
.db num_up                 ;+3
.db num_right              ;+4
.db num_down               ;+5
.db num_left               ;+6
.db element_by_enum        ;+7
.dw offst_to_image_data    ;+8
_string_data:
.db "Zero-terminated names",0
_image_data:
.db compressed_image_file
'''

print "Loading libraries"
from PIL import Image,ImageTk
import Tkinter as tk
import sys,os,subprocess,json,struct,tempfile

np  = os.path.normpath
cwd = os.getcwd()
    
TEMP_DIR     = np(cwd+"/obj/")
TEMP_PNG_DIR = np(cwd+"/obj/png")
OUTPUT_DIR   = np(cwd+"/bin")
STATUS_FILE  = np(TEMP_DIR+'/curstate')

def GETIMGPATH(fname): return np(TEMP_PNG_DIR+"/"+fname)
def GETIMGNAMES():
    global TEMP_PNG_DIR
    return sorted([f for f in os.listdir(TEMP_PNG_DIR) if os.path.isfile(os.path.join(TEMP_PNG_DIR,f))])
def ensure_dir(d):
    if not os.path.isdir(d): os.makedirs(d)
    
ensure_dir(TEMP_DIR)
ensure_dir(TEMP_PNG_DIR)
ensure_dir(OUTPUT_DIR)

enum_elements = ["none","poison","fire","wind","earth","water","ice","thunder","holy"]
enum_type = ['monster','boss','gf','player']

# =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
# Miscellaneous
def readFile(fn):
    a = []
    with open(fn,"rb") as f:
        b = f.read(1)
        while b!=b'':
            a.append(ord(b))
            b = f.read(1)
    return a
def writeFile(fn,a):
    with open(fn,"wb+") as f:
        f.write(bytearray(a))
        
# -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
# Data storage classes
class HeaderData():
    def __init__(self,header_ident,description):
        self.ident = str(header_ident)
        self.descr = str(description)
        
    def getsize(self):
        return len(self.tobytes(0))
        
    def tobytes(self,numcards):
        s  = "TriCrPak"
        s += self.ident.ljust(9,'\x00')[:9]
        s += self.descr + '\x00'
        s += struct.pack('<H',numcards)
        return s

class _carddata():
    def __init__(self,rank,name,type,u,r,d,l,element,imgname):
        self.rank = rank
        self.name = str(name)
        self.type = type
        self.up   = u
        self.down = d
        self.right= r
        self.left = l
        self.element = element
        self.imgname = str(imgname)
        
class CardCollection():
    def __init__(self):
        self.cardcount = 0
        self.cardarray = []
        
    def addcard(self,carddata_from_json):
        global enum_elements
        if len(carddata_from_json) is not 9:
            raise ValueError("Input object not valid: "+str(carddata_from_json))
        i = carddata_from_json
        if isinstance(i[7],basestring): i[7] = enum_elements.index(i[7].lower())
        if isinstance(i[2],basestring): i[2] = enum_type.index(i[2].lower())
        self.cardarray.append(_carddata(i[0],i[1],i[2],i[3],i[4],i[5],i[6],i[7],i[8]))
        self.cardcount += 1

    def getdatasize(self):
        #db rank; dw sofs, db u,r,d,l,e; dw iofs = 10 bytes
        return 10*self.cardcount
        
    def getstringsize(self):
        #len + 1 for nul
        s = 0
        for i in self.cardarray:
            s+= len(i.name)+1
        return s
            
    def getstringoffset(self,cardname):
        offset = 0
        for i in self.cardarray:
            if i.name == cardname:
                break
            offset += len(i.name)+1
        else:
            raise ValueError("Card name ["+str(cardname)+"] not found in the list.")
        return offset
        
    def tobytes(self,header,imgcoll):
        s = ''
        for i in self.cardarray:
            t  = struct.pack("B",i.rank)
            t += struct.pack("<H",header.getsize()+self.getdatasize()+self.getstringoffset(i.name))
            t += struct.pack("B",i.up)
            t += struct.pack("B",i.right)
            t += struct.pack("B",i.down)
            t += struct.pack("B",i.left)
            t += struct.pack("B",i.element)
            t += struct.pack("<H",header.getsize()+self.getdatasize()+self.getstringsize()+imgcoll.getimgoffset(i.imgname))
            s += t
        t = ''
        for i in self.cardarray:
            t += i.name + '\x00'
        s += t
        return s

class _imagedata():
    def __init__(self,imgfile,pal):
        global TEMP_DIR
        pimg = Image.new('P',(16,16))
        pimg.putpalette(pal)
        iimg = Image.open(imgfile)
        timg = quantizetopalette(iimg,pimg)
        rdata = timg.tobytes()
        r = np(TEMP_DIR+"/r")
        c = np(TEMP_DIR+"/c")
        n = open("NUL","w")
        if os.path.exists(r): os.remove(r)
        if os.path.exists(c): os.remove(c)
        writeFile(r,rdata)
        subprocess.call([np(cwd+"/tools/zx7.exe"),r,c],stdout=n)
        cdata = readFile(c)
        #
        self.name = os.path.split(imgfile)[1]
        self.rdata = rdata
        self.csize = len(self.rdata)
        self.cdata = cdata
        self.csize = len(self.cdata)
        
class ImageCollection():
    def __init__(self,pal):
        self.imgarray = []     # takes _imagedata objects
        pal += [(0,0,0)] *256
        t = []
        for i in pal[:256]:
            for j in i:
                t.append(j)
        self.imgpalette = t  # use xlibc palette, flattened
        
    def addimg(self,imagefile):
        self.imgarray.append(_imagedata(imagefile,self.imgpalette))
        
    def getimgoffset(self,imgname):
        offset = 0
        for i in self.imgarray:
            if i.name == imgname:
                break
            offset += i.csize
        else:  #executes if loop exits without breaking
            raise ValueError("Image "+str(imgname)+" not found in image data array.")
        return offset
        
    def tobytes(self):
        s = ''
        for i in self.imgarray:
            s += str(bytearray(i.cdata))
        return s


# -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
# Export data to appvar
TI_VAR_PROG_TYPE = 0x05
TI_VAR_PROTPROG_TYPE = 0x06
TI_VAR_APPVAR_TYPE = 0x15
TI_VAR_FLAG_RAM = 0x00
TI_VAR_FLAG_ARCHIVED = 0x80

def export8xv(filepath,filename,filedata):
    # Ensure that filedata is a string
    if isinstance(filedata,list): filedata = str(bytearray(filedata))
    # Add size bytes to file data as per (PROT)PROG/APPVAR data structure
    dsl = len(filedata)&0xFF
    dsh = (len(filedata)>>8)&0xFF
    filedata = str(bytearray([dsl,dsh]))+filedata
    # Construct variable header
    vsl = len(filedata)&0xFF
    vsh = (len(filedata)>>8)&0xFF
    vh  = str(bytearray([0x0D,0x00,vsl,vsh,TI_VAR_APPVAR_TYPE]))
    vh += filename.ljust(8,'\x00')[:8]
    vh += str(bytearray([0x00,TI_VAR_FLAG_ARCHIVED,vsl,vsh]))
    # Pull together variable metadata for TI8X file header
    varentry = vh + filedata
    varsizel = len(varentry)&0xFF
    varsizeh = (len(varentry)>>8)&0xFF
    varchksum = sum([ord(i) for i in varentry])
    vchkl = varchksum&0xFF
    vchkh = (varchksum>>8)&0xFF
    # Construct TI8X file header
    h  = "**TI83F*"
    h += str(bytearray([0x1A,0x0A,0x00]))
    h += "Rawr. Gravy. Steaks. Cherries!".ljust(42)[:42]  #Always makes comments exactly 42 chars wide.
    h += str(bytearray([varsizel,varsizeh]))
    h += varentry
    h += str(bytearray([vchkl,vchkh]))
    # Write data out to file
    writeFile(np(filepath+"/"+filename+".8xv"),h)
    return

# -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
# Conversion to palettized image without dithering, sourced from:
# https://stackoverflow.com/questions/29433243/convert-image-to-specific-palette-using-pil-without-dithering

def quantizetopalette(silf, palette, dither=False):
    silf.load()
    palette.load()
    if palette.mode != "P":
        raise ValueError("bad mode for palette image")
    if silf.mode != "RGB" and silf.mode != "L":
        raise ValueError(
            "only RGB or L mode images can be quantized to a palette"
            )
    im = silf.im.convert("P", 1 if dither else 0, palette.im)
    try:
        return silf._new(im)
    except AttributeError:
        return silf._makeself(im)
        
# -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
def usage(errorcode=0):
          #012345678901234567890123456789012345678901234567890123456789012345678901234567
    print ""
    if errorcode: print "[DEBUG] ERRCODE:"+str(errorcode)
    print "tkit.py is a TRICARDS card pack builder."
    print "Usage: python tkit.py <input_folder_path> <PACKNAME>"
    print ""
    print "<input_folder_path> must contain image files (.PNG) and"
    print "exactly one JSON-encoded file (.json) that contains all"
    print "the stats of all the cards in the card pack."
    print ""
    return 2

def fatal(errorcode):
    print "Fatal error [code: "+str(errorcode)+"] has occurred. The script will now exit."
    sys.exit(2)
# =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
# It's time.

ifpath = sys.argv[1]
ofname = sys.argv[2]
outfilename = np(OUTPUT_DIR+"/"+ofname+".8xv")
if not os.path.isdir(ifpath): sys.exit(usage(1))
if not ofname: sys.exit(usage(2))
if os.path.isfile(outfilename): os.remove(outfilename)

#Generate xLIBC palette for use in quantization
xlcpal = []
for i in range(256):
    t = i+i*256
    r,g,b = (((t>>10)&0x1F)<<3,((t>>5)&0x1F)<<3,(t&0x1F)<<3)
    xlcpal.append((r,g,b))

#Collect data    
imlist = [i for i in os.listdir(ifpath) if i.endswith(".png")]
imdata = [i for i in os.listdir(ifpath) if i.endswith(".json")]
if len(imdata)!=1: fatal("JSON_NEQ_1")
carddata = None
with open(np(ifpath+"/"+imdata[0]),'r') as f:
    carddata = json.load(f)
header = HeaderData(carddata[1],carddata[0])
carddata = carddata[2:]
ccoll = CardCollection()
icoll = ImageCollection(xlcpal)
for i in carddata: ccoll.addcard(i)
for i in imlist:   icoll.addimg(np(ifpath+'/'+i))
    
data = header.tobytes(ccoll.cardcount) + ccoll.tobytes(header,icoll) + icoll.tobytes()
#with open("temp",'wb') as f: f.write(data)
export8xv(OUTPUT_DIR,ofname,data)
print "File "+str(ofname)+" built at "+str(OUTPUT_DIR)
print "Hdr "+str(header.getsize())+" bytes, cdat "+str(ccoll.getdatasize())+" bytes, ndat "+str(ccoll.getstringsize())+" bytes, cidat "+str(len(icoll.tobytes()))+" bytes"





