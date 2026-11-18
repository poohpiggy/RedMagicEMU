import ctypes as C
exec(open("egl_probe.py").read())
L=C.CDLL("./libpm.so")
UP=C.c_ulonglong; IP=C.c_ssize_t; UI=C.c_uint; PI=C.POINTER(UI); PW=C.POINTER(UP)
geterr=proc("glGetError",UI,[]);mapgl=proc("glMapBufferRange",P,[UI,IP,IP,UI]);getptr=proc("glGetBufferPointerv",None,[UI,UI,C.POINTER(P)]);fl=proc("glFlushMappedBufferRange",None,[UI,IP,IP]);un=proc("glUnmapBuffer",C.c_ubyte,[UI])
gen=proc("glGenBuffers",None,[I,PI]);bind=proc("glBindBuffer",None,[UI,UI]);alloc=proc("glBufferData",None,[UI,IP,P,UI]);flushgl=proc("glFlush",None,[])
L.pm_new.argtypes=[P,P,P,P,P];L.pm_new.restype=P
L.pm_ctx_new.argtypes=[P,UP];L.pm_ctx_new.restype=P
L.pm_map.argtypes=[P,UI,IP,IP,UI,PW];L.pm_map.restype=I
L.pm_flush.argtypes=[P,UI,UP,IP,P,IP];L.pm_flush.restype=I
L.pm_unmap.argtypes=[P,UI,UP];L.pm_unmap.restype=I
L.pm_error.argtypes=[P];L.pm_error.restype=UI
L.pm_ctx_free.argtypes=[P];L.pm_free.argtypes=[P];L.pm_free.restype=I
b=L.pm_new(*[C.cast(x,P) for x in (geterr,mapgl,getptr,fl,un)]);assert b
c1=L.pm_ctx_new(b,1);assert c1
T=0x8F37;R=0x8F36; ids=(UI*2)();gen(2,ids)
for x in ids: bind(T,x);alloc(T,64,None,0x88E8)
assert geterr()==0
handles=[]
for x in ids:
 bind(T,x);h=UP();rc=L.pm_map(c1,T,8,32,0x36,C.byref(h));assert rc==0,("first/second real map",rc);handles.append(h.value)
assert handles[0] and handles[1] and handles[0]!=handles[1]
# Cannot use A handle against currently bound B.
a=C.create_string_buffer(b"ABCD");assert L.pm_flush(c1,T,handles[0],0,a,4)!=0
# Same object can be flushed through a different target.
bind(R,ids[0]);assert L.pm_flush(c1,R,handles[0],2,a,4)==0
bind(T,ids[1]);bb=C.create_string_buffer(b"WXYZ");assert L.pm_flush(c1,T,handles[1],1,bb,4)==0
# Previously outstanding context error must remain visible after valid RPC.
bind(0xDEADBEEF,0);assert L.pm_flush(c1,T,handles[1],5,bb,4)==0;assert L.pm_error(c1)==0x500
# Shared context must observe the same host mapping ID.
c2=e.eglCreateContext(d,cfg,c,ca);assert c2;assert e.eglMakeCurrent(d,s,s,c2);second=L.pm_ctx_new(b,1);assert second
bind(T,ids[0]);assert L.pm_flush(second,T,handles[0],7,a,4)==0;assert L.pm_unmap(second,T,handles[0])==0
bind(T,ids[1]);assert L.pm_unmap(second,T,handles[1])==0
# No false success for nonexistent mapping.
assert L.pm_flush(second,T,handles[1],0,a,4)!=0
# Real host map failure is retained for guest GL error.
bind(T,ids[0]);bad=UP();assert L.pm_map(second,T,63,8,0x36,C.byref(bad))!=0;assert L.pm_error(second)==0x501
L.pm_ctx_free(second);L.pm_ctx_free(c1);assert L.pm_free(b)==0
print("HOST_MAP_PROTOCOL_REAL_GL_OK")
